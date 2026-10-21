/*
 * Pixel 6a (bluejay) boot shim: acts as iBoot for the SWEET xnu.
 *
 * Pixel ABL loads this as a Linux arm64 Image (fastboot boot, boot.img v4)
 * and enters it with the MMU and D-cache off, x0 = flattened device tree,
 * normally at EL2. The shim:
 *   1. drops to EL1 (XNU expects EL1, like iBoot leaves it),
 *   2. finds the SWEETXNU payload (tools/mkpayload.py) appended to this Image
 *      (header fields 0x20/0x28, set by tools/mkbootimg.py), or in the initrd,
 *   3. checks the live FDT: XNU's RAM range inside /memory and clear of every
 *      reserved region, the splash framebuffer where the kernel expects it,
 *   4. stops any watchdog ABL left running, sets up an optional UART,
 *   5. copies the kernel segments, ADT and boot_args to the upper DRAM bank
 *      and jumps to XNU at EL1, MMU off, x0 = boot_args.
 * Without a payload (or with "shimdiag" on ABL's or the payload's command
 * line) it runs the diagnostic screen of docs/bringup.md instead.
 *
 * Built as one position-independent blob with no relocations (build.sh):
 * no pointers in static data, everything addressed PC-relative.
 */
#include "board.h"
#include "font.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

/* ------------------------------------------------------------------ entry */

__asm__(
	".text\n"
	"_head:\n"
	"	b	_start\n"			/* code0 */
	"	.long	0\n"				/* code1 */
	"	.quad	0\n"				/* text_offset */
	"	.quad	0\n"				/* image_size: tools/mkbootimg.py */
	"	.quad	0xA\n"				/* LE, 4K pages, anywhere */
	"	.quad	0\n"				/* res2: payload offset (mkbootimg.py) */
	"	.quad	0\n"				/* res3: payload size */
	"	.quad	0\n"				/* res4 */
	"	.ascii	\"ARM\\x64\"\n"
	"	.long	0\n"
	"_start:\n"
	"	msr	daifset, #0xf\n"
	"	mov	x19, x0\n"			/* FDT */
	"	adr	x20, _head\n"
	"	mrs	x21, CurrentEL\n"
	"	ubfx	x21, x21, #2, #2\n"
	"	cmp	x21, #2\n"
	"	b.ne	1f\n"
	/* EL2 -> EL1. EL1 must see the real IDs, own the timer and the GIC
	 * system registers, and run without stage 2 or EL2 traps. */
	"	mrs	x9, midr_el1\n"
	"	msr	vpidr_el2, x9\n"
	"	mrs	x9, mpidr_el1\n"
	"	msr	vmpidr_el2, x9\n"
	"	mov	x9, #(1 << 31)\n"		/* HCR_EL2.RW */
	"	msr	hcr_el2, x9\n"
	"	mov	x9, #0x33ff\n"			/* CPTR_EL2: RES1 bits, no traps */
	"	msr	cptr_el2, x9\n"
	"	msr	hstr_el2, xzr\n"
	"	mrs	x9, pmcr_el0\n"
	"	ubfx	x9, x9, #11, #5\n"		/* MDCR_EL2.HPMN = PMCR.N */
	"	msr	mdcr_el2, x9\n"
	"	mov	x9, #3\n"			/* EL1PCTEN | EL1PCEN */
	"	msr	cnthctl_el2, x9\n"
	"	msr	cntvoff_el2, xzr\n"
	"	msr	vttbr_el2, xzr\n"
	"	mrs	x9, id_aa64pfr0_el1\n"
	"	ubfx	x9, x9, #24, #4\n"		/* GIC system register interface? */
	"	cbz	x9, 2f\n"
	"	mrs	x9, S3_4_C12_C9_5\n"		/* ICC_SRE_EL2 */
	"	orr	x9, x9, #0x1\n"			/* SRE */
	"	orr	x9, x9, #0x8\n"			/* Enable: EL1 may use SRE */
	"	msr	S3_4_C12_C9_5, x9\n"
	"	isb\n"
	"2:	movz	x9, #0x0800\n"
	"	movk	x9, #0x30d0, lsl #16\n"		/* SCTLR_EL1: RES1, MMU/caches off */
	"	msr	sctlr_el1, x9\n"
	"	adr	x9, 1f\n"
	"	msr	elr_el2, x9\n"
	"	mov	x9, #0x3c5\n"			/* EL1h, DAIF masked */
	"	msr	spsr_el2, x9\n"
	"	isb\n"
	"	eret\n"
	"1:	adr	x9, stack_top\n"
	"	mov	sp, x9\n"
	"	mrs	x9, CurrentEL\n"
	"	cmp	x9, #4\n"			/* EL1: our vectors */
	"	b.ne	3f\n"
	"	adr	x9, vectors\n"
	"	msr	vbar_el1, x9\n"
	"	isb\n"
	"3:	mov	x0, x19\n"
	"	mov	x1, x20\n"
	"	mov	x2, x21\n"
	"	bl	shim_main\n"
	"4:	wfe\n"
	"	b	4b\n"

	".balign 2048\n"
	"vectors:\n"
	".irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"
	"	.balign 128\n"
	"	mov	x0, #\\i\n"
	"	b	exc_common\n"
	".endr\n"
	"exc_common:\n"
	"	adr	x9, stack_top\n"
	"	mov	sp, x9\n"
	"	mrs	x1, esr_el1\n"
	"	mrs	x2, elr_el1\n"
	"	mrs	x3, far_el1\n"
	"	adr	x4, _head\n"
	"	bl	shim_fault\n"
	"5:	wfe\n"
	"	b	5b\n"
);

__asm__(
	".text\n"
	".balign 16\n"
	"stack_bottom:\n"
	"	.zero	65536\n"
	"stack_top:\n"
);

/* ------------------------------------------------------------------ basics */

#define rsr(r) ({ u64 _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static inline u32 rd32(u64 a) { return *(volatile u32 *)a; }
static inline void wr32(u64 a, u32 v) { *(volatile u32 *)a = v; __asm__ volatile("dsb sy" ::: "memory"); }

/* The compiler may emit calls to these; MMU off means Device memory, so stay aligned. */
void *memset(void *d, int c, unsigned long n)
{
	volatile u8 *p = d;
	while (n--)
		*p++ = (u8)c;
	return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
	volatile u8 *dp = d;
	const volatile u8 *sp = s;
	while (n--)
		*dp++ = *sp++;
	return d;
}

static int str_len(const char *s)
{
	int n = 0;
	while (s[n])
		n++;
	return n;
}

static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int starts(const char *s, const char *prefix)
{
	while (*prefix)
		if (*s++ != *prefix++)
			return 0;
	return 1;
}

/* Is needle somewhere in the first n bytes of hay (NULs inside allowed: string lists)? */
static int contains(const char *hay, u32 n, const char *needle)
{
	int m = str_len(needle);
	for (u32 i = 0; i + m <= n; i++) {
		int k = 0;
		while (k < m && hay[i + k] == needle[k])
			k++;
		if (k == m)
			return 1;
	}
	return 0;
}

static u64 timer_hz;

static u64 now(void)
{
	__asm__ volatile("isb" ::: "memory");
	return rsr("cntpct_el0");
}

static void delay_ms(u64 ms)
{
	u64 t0 = now();
	while (now() - t0 < timer_hz / 1000 * ms)
		;
}

static u64 smc(u64 fn, u64 a1, u64 a2, u64 a3)
{
	register u64 x0 __asm__("x0") = fn;
	register u64 x1 __asm__("x1") = a1;
	register u64 x2 __asm__("x2") = a2;
	register u64 x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :
	    : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	    "x16", "x17", "memory");
	return x0;
}

/* ------------------------------------------------------------------ UART */

enum { UART_NONE, UART_PL011, UART_SAMSUNG };
static int uart_kind;
static u64 uart_base;

static void uart_putc(char c)
{
	if (uart_kind == UART_PL011) {
		for (int i = 0; i < 100000 && (rd32(uart_base + 0x18) & 0x20); i++)
			;
		*(volatile u32 *)uart_base = (u8)c;
	} else if (uart_kind == UART_SAMSUNG) {
		/* FIFO mode: UFSTAT TX full (bit 24); else UTRSTAT TX buffer empty (bit 1) */
		int fifo = rd32(uart_base + 0x08) & 1;
		for (int i = 0; i < 100000; i++)
			if (fifo ? !(rd32(uart_base + 0x18) & (1u << 24)) : (rd32(uart_base + 0x10) & 2))
				break;
		*(volatile u32 *)(uart_base + 0x20) = (u8)c;
	}
}

/* ------------------------------------------------------------------ screen */

#define SCALE		2
#define CELL_W		(8 * SCALE)
#define CELL_H		(16 * SCALE)
#define C_BG		0xff101830u
#define C_FG		0xffffffffu
#define C_OK		0xff30e070u
#define C_DIM		0xff8090b0u
#define C_WARN		0xffffc040u
#define C_ERR		0xffe03030u

static u64 fb_base = FB_BASE;
static int row, col;
static u32 color = C_FG;

static void fill_rows(int y0, int y1, u32 c)
{
	for (int y = y0; y < y1 && y < FB_H; y++) {
		volatile u32 *line = (volatile u32 *)(fb_base + (u64)y * FB_STRIDE);
		for (int x = 0; x < FB_W; x++)
			line[x] = c;
	}
}

static void put_glyph(int r, int c, char ch)
{
	const unsigned char *g = font8x16[(ch < 0x20 || ch > 0x7e ? '?' : ch) - 0x20];
	for (int gy = 0; gy < 16; gy++)
		for (int sy = 0; sy < SCALE; sy++) {
			volatile u32 *line = (volatile u32 *)(fb_base +
			    (u64)(r * CELL_H + gy * SCALE + sy) * FB_STRIDE) + c * CELL_W;
			for (int gx = 0; gx < 8; gx++) {
				u32 px = (g[gy] & (0x80 >> gx)) ? color : C_BG;
				for (int sx = 0; sx < SCALE; sx++)
					line[gx * SCALE + sx] = px;
			}
		}
}

static void newline(void)
{
	uart_putc('\r');
	uart_putc('\n');
	col = 0;
	row = (row + 1) % (FB_H / CELL_H);
	fill_rows(row * CELL_H, (row + 1) * CELL_H, C_BG);
}

static void putch(char ch)
{
	if (ch == '\n') {
		newline();
		return;
	}
	if (col == FB_W / CELL_W)
		newline();
	put_glyph(row, col++, ch);
	uart_putc(ch);
}

static void puts_(const char *s)
{
	while (*s)
		putch(*s++);
}

static void puthex(u64 v, int digits)
{
	putch('0');
	putch('x');
	for (int i = digits - 1; i >= 0; i--)
		putch("0123456789abcdef"[(v >> (4 * i)) & 0xf]);
}

static void say(u32 c, const char *s)
{
	color = c;
	puts_(s);
	newline();
	color = C_FG;
}

static void kv(const char *label, u64 v)
{
	puts_(label);
	puthex(v, v >> 32 ? 16 : 8);
	newline();
}

static void kv2(const char *label, u64 a, const char *label2, u64 b)
{
	puts_(label);
	puthex(a, a >> 32 ? 16 : 8);
	puts_(label2);
	puthex(b, b >> 32 ? 16 : 8);
	newline();
}

static void __attribute__((noreturn)) halt(const char *why)
{
	say(C_ERR, why);
	say(C_DIM, "stopped. Power + Vol- (hold) = bootloader");
	for (;;)
		__asm__ volatile("wfe");
}

__attribute__((used)) void shim_fault(u64 vec, u64 esr, u64 elr, u64 far, u64 base)
{
	color = C_ERR;
	puts_("EXCEPTION vec ");
	puthex(vec, 2);
	newline();
	kv("  ESR ", esr);
	kv2("  ELR-image ", elr - base, " FAR ", far);
	halt("exception in the shim");
}

/* ------------------------------------------------------------------ FDT */

#define FDT_MAGIC	0xd00dfeed
#define MAXD		8

static u32 be32(u64 a)
{
	return __builtin_bswap32(rd32(a));
}

static u64 cells(u64 a, u32 n)
{
	return n == 2 ? (u64)be32(a) << 32 | be32(a + 4) : be32(a);
}

/* Walks every property of the tree in order. */
struct fdt {
	u64 base, strs, p;
	int depth;			/* root node = 1 */
	u64 node[MAXD];			/* name of the node at each depth */
	u64 start[MAXD];		/* first token of each node's body */
	u32 ac[MAXD], sc[MAXD];		/* #address/#size-cells for that node's children */
	u64 name, val;			/* current property */
	u32 len;
};

static int fdt_ok(u64 dtb)
{
	return dtb && !(dtb & 3) && be32(dtb) == FDT_MAGIC;
}

static void fdt_begin(struct fdt *f, u64 dtb)
{
	f->base = dtb;
	f->strs = dtb + be32(dtb + 12);
	f->p = dtb + be32(dtb + 8);
	f->depth = 0;
}

static u64 skip_str(u64 p)
{
	while (*(volatile char *)p)
		p++;
	return (p + 1 + 3) & ~3UL;
}

static int fdt_next(struct fdt *f)
{
	for (;;) {
		u32 tok = be32(f->p);
		f->p += 4;
		if (tok == 1) {			/* BEGIN_NODE */
			if (f->depth + 1 >= MAXD)
				return 0;
			f->depth++;
			f->node[f->depth] = f->p;
			f->p = skip_str(f->p);
			f->start[f->depth] = f->p;
			f->ac[f->depth] = 2;
			f->sc[f->depth] = 1;
		} else if (tok == 2) {		/* END_NODE */
			if (--f->depth < 0)
				return 0;
		} else if (tok == 3) {		/* PROP */
			f->len = be32(f->p);
			f->name = f->strs + be32(f->p + 4);
			f->val = f->p + 8;
			f->p = (f->val + f->len + 3) & ~3UL;
			const char *n = (const char *)f->name;
			if (f->len == 4 && str_eq(n, "#address-cells"))
				f->ac[f->depth] = be32(f->val);
			else if (f->len == 4 && str_eq(n, "#size-cells"))
				f->sc[f->depth] = be32(f->val);
			return 1;
		} else if (tok != 4) {		/* NOP; anything else = END */
			return 0;
		}
	}
}

static const char *node_name(struct fdt *f, int d)
{
	return (const char *)f->node[d];
}

static int prop_is(struct fdt *f, const char *n)
{
	return str_eq((const char *)f->name, n);
}

/* Look up another property of the node the walker is in (its own props only). */
static int node_prop(struct fdt *f, const char *want, u64 *val, u32 *len)
{
	u64 p = f->start[f->depth];
	for (;;) {
		u32 tok = be32(p);
		if (tok == 4) {
			p += 4;
			continue;
		}
		if (tok != 3)
			return 0;
		u32 l = be32(p + 4);
		if (str_eq((const char *)(f->strs + be32(p + 8)), want)) {
			*val = p + 12;
			*len = l;
			return 1;
		}
		p = (p + 12 + l + 3) & ~3UL;
	}
}

/* First <address, size> of this node's reg, using the parent's cell counts. */
static int node_reg(struct fdt *f, int index, u64 *addr, u64 *size)
{
	u64 v;
	u32 len, ac = f->ac[f->depth - 1], sc = f->sc[f->depth - 1];
	if (!node_prop(f, "reg", &v, &len) || ac < 1 || ac > 2 || sc > 2 || (index + 1) * (ac + sc) * 4 > len)
		return 0;
	v += index * (ac + sc) * 4;
	*addr = cells(v, ac);
	*size = sc ? cells(v + ac * 4, sc) : 0;
	return 1;
}

static int node_compatible(struct fdt *f, const char *needle)
{
	u64 v;
	u32 len;
	return node_prop(f, "compatible", &v, &len) && contains((const char *)v, len, needle);
}

static int chosen_prop(u64 dtb, const char *name, u64 *val, u32 *len)
{
	struct fdt f;
	fdt_begin(&f, dtb);
	while (fdt_next(&f))
		if (f.depth == 2 && str_eq(node_name(&f, 2), "chosen") && prop_is(&f, name)) {
			*val = f.val;
			*len = f.len;
			return 1;
		}
	return 0;
}

static int chosen_u64(u64 dtb, const char *name, u64 *out)
{
	u64 v;
	u32 len;
	if (!chosen_prop(dtb, name, &v, &len) || (len != 4 && len != 8))
		return 0;
	*out = cells(v, len / 4);
	return 1;
}

/* ------------------------------------------------------------------ payload */

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	u64 vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	u32 version, nsegs;
	u64 virt_base, entry_va, adt_off, adt_size, kernel_end;
	u64 phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_args {
	u16 revision, version;
	u64 virt_base, phys_base, mem_size, top_of_kernel_data;
	u64 v_base, v_display, v_row_bytes, v_width, v_height, v_depth;
	u32 machine_type;
	u64 device_tree;
	u32 device_tree_length;
	char command_line[BOOT_LINE_LENGTH];
	u64 boot_flags, mem_size_actual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header");
_Static_assert(__builtin_offsetof(struct boot_args, v_base) == 40, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, command_line) == 108, "boot_args");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args");

static int is_payload(u64 a)
{
	return rd32(a) == 0x45455753 && rd32(a + 4) == 0x554e5854;	/* "SWEETXNU" */
}

/* Image-embedded payload first (always intact), then a scan of the initrd. */
static u64 find_payload(u64 dtb, u64 base, u64 *size, const char **where)
{
	u64 off = *(volatile u64 *)(base + 0x20), sz = *(volatile u64 *)(base + 0x28);
	if (off && sz && !(off & 7) && is_payload(base + off)) {
		*size = sz;
		*where = "appended to the Image";
		return base + off;
	}
	u64 lo, hi;
	if (fdt_ok(dtb) && chosen_u64(dtb, "linux,initrd-start", &lo) && chosen_u64(dtb, "linux,initrd-end", &hi))
		for (u64 a = (lo + 0xfff) & ~0xfffUL; a + 0x2a8 < hi; a += 0x1000)
			if (is_payload(a)) {
				*size = hi - a;
				*where = "in the initrd";
				return a;
			}
	return 0;
}

static int payload_flag(const struct payload *p, const char *flag)
{
	return p && contains(p->cmdline, BOOT_LINE_LENGTH, flag);
}

/* ------------------------------------------------------------------ checks */

static int overlaps(u64 a, u64 as, u64 b, u64 bs)
{
	return a < b + bs && b < a + as;
}

/* XNU's RAM must be inside one /memory range and clear of every reservation. */
static int check_memory(u64 dtb, u64 base, u64 size, int verbose)
{
	struct fdt f;
	int inside = 0, bad = 0, nmem = 0;
	u64 mem_a[16], mem_s[16];
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		if (!prop_is(&f, "reg"))
			continue;
		if (f.depth == 2 && starts(node_name(&f, 2), "memory")) {
			for (int i = 0; i < 8; i++) {
				u64 a, s;
				if (!node_reg(&f, i, &a, &s))
					break;
				if (verbose)
					kv2("  memory ", a, " size ", s);
				if (nmem < 16) {
					mem_a[nmem] = a;
					mem_s[nmem++] = s;
				}
			}
		} else if (f.depth == 3 && str_eq(node_name(&f, 2), "reserved-memory")) {
			for (int i = 0; i < 8; i++) {
				u64 a, s;
				if (!node_reg(&f, i, &a, &s))
					break;
				if (overlaps(a, s, base, size)) {
					color = C_ERR;
					puts_("  reserved ");
					puts_(node_name(&f, 3));
					puts_(" overlaps XNU RAM at ");
					puthex(a, 16);
					newline();
					color = C_FG;
					bad = 1;
				} else if (verbose && a >= 0x100000000UL) {
					puts_("  high reserved ");
					puts_(node_name(&f, 3));
					puts_(" ");
					puthex(a, 16);
					newline();
				}
			}
		}
	}
	/* /memreserve/ block */
	for (u64 r = dtb + be32(dtb + 16);; r += 16) {
		u64 a = cells(r, 2), s = cells(r + 8, 2);
		if (!a && !s)
			break;
		if (overlaps(a, s, base, size)) {
			kv("  memreserve overlaps XNU RAM: ", a);
			bad = 1;
		}
	}
	/* Pixel ABL lists the upper bank as adjacent 2 GiB ranges: walk across them. */
	u64 end = base;
	for (int pass = 0; pass < nmem && end < base + size; pass++)
		for (int i = 0; i < nmem; i++)
			if (mem_a[i] <= end && end < mem_a[i] + mem_s[i])
				end = mem_a[i] + mem_s[i];
	inside = end >= base + size;
	if (!inside)
		say(C_ERR, "  XNU RAM is not inside the /memory ranges");
	return inside && !bad;
}

/* Where the live tree says the splash buffer is (0 if it says nothing). */
static u64 fdt_framebuffer(u64 dtb, int verbose)
{
	struct fdt f;
	u64 simple = 0, splash = 0;
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		if (!prop_is(&f, "compatible") && !prop_is(&f, "reg"))
			continue;
		u64 a, s;
		if (prop_is(&f, "compatible") && contains((const char *)f.val, f.len, "simple-framebuffer") &&
		    node_reg(&f, 0, &a, &s)) {
			simple = a;
			if (verbose)
				kv2("  simple-framebuffer ", a, " size ", s);
		} else if (prop_is(&f, "reg") && f.depth == 3 && str_eq(node_name(&f, 2), "reserved-memory") &&
		    (contains(node_name(&f, 3), 64, "splash") || contains(node_name(&f, 3), 64, "framebuffer") ||
		    contains(node_name(&f, 3), 64, "fb")) && node_reg(&f, 0, &a, &s)) {
			if (!splash)
				splash = a;
			if (verbose) {
				puts_("  reserved ");
				puts_(node_name(&f, 3));
				puts_(" ");
				puthex(a, 16);
				newline();
			}
		}
	}
	return simple ? simple : splash;
}

/* Stop every Samsung watchdog in the tree (WTCON = 0). */
static void watchdogs_off(u64 dtb, int verbose)
{
	struct fdt f;
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		u64 a, s;
		if (!prop_is(&f, "compatible") || !contains((const char *)f.val, f.len, "wdt") ||
		    !starts(node_name(&f, f.depth), "watchdog") || !node_reg(&f, 0, &a, &s))
			continue;
		u32 before = rd32(a);
		if (before & (1u << 5))
			wr32(a, 0);
		if (verbose || (before & (1u << 5)))
			kv2("  watchdog ", a, " WTCON was ", before), kv("    now ", rd32(a));
	}
}

/*
 * GS101 DECON0: report its mode and, in command mode with a masked hardware
 * trigger, unmask it so every TE pulse sends the framebuffer to the panel
 * (continuous refresh). Only when the live tree has the DECON at DECON0.
 */
static int fdt_has_decon(u64 dtb)
{
	struct fdt f;
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		u64 a, s;
		if (prop_is(&f, "compatible") && contains((const char *)f.val, f.len, "exynos-decon") &&
		    node_reg(&f, 0, &a, &s) && a == DECON0)
			return 1;
	}
	return 0;
}

static void decon_refresh(u64 dtb, int fix)
{
	if (!fdt_has_decon(dtb)) {
		if (fix < 0)
			say(C_DIM, "  no GS101 DECON in the device tree");
		return;
	}
	u32 g = rd32(DECON0 + DECON_GLOBAL_CON), t = rd32(DECON0 + DECON_TRIG_CON);
	kv2("  DECON GLOBAL_CON ", g, " TRIG_CON ", t);
	int cmd = (g >> 8) & 1, on = g & 3, sw = (t >> 8) & 1;
	if (!on || !cmd) {
		say(C_DIM, on ? "  video mode: refreshes by itself" : "  DECON off?");
		return;
	}
	if (sw) {
		say(C_WARN, "  command mode, SOFTWARE trigger: needs a kick per frame");
		return;
	}
	if ((t & 1) && !(t & 0x10)) {
		say(C_OK, "  command mode, TE trigger unmasked: refreshes by itself");
		return;
	}
	if (!fix) {
		say(C_WARN, "  command mode, TE trigger masked (not changed)");
		return;
	}
	wr32(DECON0 + DECON_SHD_REG_UP_REQ, 1u << 31);
	wr32(DECON0 + DECON_TRIG_CON, (t & ~0x10u) | 1u);
	kv("  TE trigger unmasked, TRIG_CON now ", rd32(DECON0 + DECON_TRIG_CON));
}

/* PL011 always (QEMU); the phone's Samsung UART only when ABL enabled it. */
static void uart_setup(u64 dtb, const struct payload *p)
{
	struct fdt f;
	u64 v;
	u32 len;
	int abl_console = chosen_prop(dtb, "bootargs", &v, &len) &&
	    (contains((const char *)v, len, "ttySAC") || contains((const char *)v, len, "earlycon"));
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		u64 a, s;
		if (!prop_is(&f, "compatible") || !node_reg(&f, 0, &a, &s))
			continue;
		const char *c = (const char *)f.val;
		if (contains(c, f.len, "arm,pl011")) {
			uart_kind = UART_PL011;
			uart_base = a;
			return;
		}
		if ((contains(c, f.len, "exynos4210-uart") || contains(c, f.len, "gs101-uart") ||
		    contains(c, f.len, "exynos-uart")) && a == SAMSUNG_UART &&
		    (abl_console || payload_flag(p, "sweetuart=1"))) {
			uart_kind = UART_SAMSUNG;
			uart_base = a;
			return;
		}
	}
}

/* ------------------------------------------------------------------ diag */

static void diag_ufs(u64 dtb)
{
	struct fdt f;
	fdt_begin(&f, dtb);
	while (fdt_next(&f)) {
		u64 a, s, vs, vss;
		if (!prop_is(&f, "compatible") || !contains((const char *)f.val, f.len, "ufs") ||
		    !(contains((const char *)f.val, f.len, "exynos") || contains((const char *)f.val, f.len, "gs101")) ||
		    !node_reg(&f, 0, &a, &s))
			continue;
		puts_("  ");
		puts_(node_name(&f, f.depth));
		newline();
		kv2("    CAP ", rd32(a + 0x00), " VER ", rd32(a + 0x08));
		kv2("    HCS ", rd32(a + 0x30), " HCE ", rd32(a + 0x34));
		kv2("    UTRLBA ", rd32(a + 0x50) | (u64)rd32(a + 0x54) << 32, " DBR ", rd32(a + 0x58));
		if (node_reg(&f, 1, &vs, &vss)) {
			kv2("    vs TXPRDT ", rd32(vs + 0x00), " RXPRDT ", rd32(vs + 0x04));
			kv("    vs UTRL_NEXUS ", rd32(vs + 0x40));
		}
		return;
	}
	say(C_DIM, "  no Exynos UFS node");
}

/* ---- ACPM: PMIC register access through the APM firmware (board.h) ---- */

static u8 acpm_seq;

/*
 * One request on ACPM channel chan. cmd[0] bits 21:16 receive the sequence
 * number; the firmware echoes it in its reply, whose second word goes to
 * *rx1. Returns 0, or a negative code.
 */
static int acpm_xfer(u32 chan, u32 cmd[4], u32 *rx1)
{
	u64 shmem = APM_SRAM + ACPM_INITDATA;
	if (rd32(shmem + 24) <= chan)			/* num_chans */
		return -1;
	u64 ch = APM_SRAM + rd32(shmem + 8) + chan * 72;	/* acpm_chan_shmem */
	u32 qlen = rd32(ch + 52), mlen = rd32(ch + 56);
	if (!qlen || qlen > 64 || mlen < 16 || mlen > 256)
		return -2;
	/* the AP sends on the firmware's rx queue and receives on its tx queue */
	u64 tx_rear = APM_SRAM + rd32(ch + 16), tx_front = APM_SRAM + rd32(ch + 20), tx_base = APM_SRAM + rd32(ch + 24);
	u64 rx_rear = APM_SRAM + rd32(ch + 40), rx_front = APM_SRAM + rd32(ch + 44), rx_base = APM_SRAM + rd32(ch + 48);

	u32 front = rd32(tx_front), next = (front + 1) % qlen;
	for (int i = 0; rd32(tx_rear) == next; i++) {
		if (i > 1000)
			return -3;			/* queue full */
		delay_ms(1);
	}
	acpm_seq = acpm_seq % 63 + 1;
	cmd[0] = (cmd[0] & ~(0x3fu << 16)) | (u32)acpm_seq << 16;
	for (int i = 0; i < 4; i++)
		wr32(tx_base + mlen * front + 4 * i, cmd[i]);
	wr32(tx_front, next);
	wr32(AP2APM_MBOX + MBOX_INTGR1, 1u << chan);

	for (int t = 0; t < 2000; t++) {		/* ~100 ms */
		u32 i = rd32(rx_rear), f = rd32(rx_front);
		int got = 0;
		u32 w1 = 0;
		while (i != f) {
			u64 m = rx_base + mlen * i;
			if (((rd32(m) >> 16) & 0x3f) == acpm_seq) {
				got = 1;
				w1 = rd32(m + 4);
			}
			i = (i + 1) % qlen;
		}
		wr32(rx_rear, f);
		if (got) {
			if (rx1)
				*rx1 = w1;
			return 0;
		}
		u64 t0 = now();
		while (now() - t0 < timer_hz / 20000)	/* 50 us */
			;
	}
	return -4;					/* no answer */
}

/*
 * One PMIC request on ACPM channel 2: func 0 = read, 2 = update(value, mask).
 * Returns 0 and the register value in *out, or a negative code.
 */
static int acpm_pmic(u8 reg, u8 func, u8 value, u8 mask, u8 *out)
{
	u32 cmd[4], w1 = 0;
	cmd[0] = (0u << 12) | (1u << 8) | reg;		/* speedy 0, type PMIC */
	cmd[1] = (u32)mask << 16 | (u32)value << 8 | func;
	cmd[2] = 0;
	cmd[3] = (u32)(now() / (timer_hz / 1000));
	int e = acpm_xfer(ACPM_PMIC_CHAN, cmd, &w1);
	if (e)
		return e;
	if (out)
		*out = (u8)(w1 >> 8);
	return (w1 >> 24) ? -(int)(w1 >> 24) - 10 : 0;
}

/*
 * CPU cluster clocks through ACPM DVFS (mainline exynos-acpm-dvfs.c,
 * clk-acpm.c: channel 0; domains 2/3/4 = CPUCL0 A55, CPUCL1 A76, CPUCL2 X1).
 * The firmware picks the operating point and its voltage. khz 0 = query.
 */
static int acpm_dvfs(u32 domain, u32 khz, u32 *khz_out)
{
	u32 cmd[4], w1 = 0;
	cmd[0] = domain & 0xfff;
	cmd[1] = khz;
	cmd[2] = khz ? ACPM_DVFS_FREQ_REQ : ACPM_DVFS_FREQ_GET;
	cmd[3] = (u32)(now() / (timer_hz / 1000));
	int e = acpm_xfer(ACPM_DVFS_CHAN, cmd, &w1);
	if (!e && khz_out)
		*khz_out = w1;
	return e;
}

/*
 * ABL leaves every cluster near 1.1-1.2 GHz (measured with sweetcpu_mhz=1).
 * shimcpu=1: A55 1.8, A76 2.25, X1 2.25 GHz; shimcpu=2: X1 2.8 GHz too.
 * XNU has no thermal management: shimcpu=2 can run hot under sustained load.
 */
static void cpu_clocks(int level, int verbose)
{
	static const u32 khz[2][3] = {
		{ 1803000, 2253000, 2253000 },
		{ 1803000, 2253000, 2802000 },
	};
	static const char *const names[3] = { "  A55 ", "  A76 ", "  X1  " };
	for (int c = 0; c < 3; c++) {
		u32 before = 0, after = 0;
		int e = acpm_dvfs(ACPM_DVFS_CPUCL0 + c, 0, &before);
		if (!e)
			e = acpm_dvfs(ACPM_DVFS_CPUCL0 + c, khz[level - 1][c], 0);
		if (!e)
			e = acpm_dvfs(ACPM_DVFS_CPUCL0 + c, 0, &after);
		if (verbose || e) {
			puts_(names[c]);
			puthex(before, 8);
			puts_(" -> ");
			puthex(after, 8);
			puts_(" kHz");
			newline();
		}
		if (e)
			kv("  ACPM DVFS error ", (u32)-e);
	}
}

/* Switch on the touch supplies, keeping a firmware voltage when in the rail's range. */
static int touch_power(int verbose)
{
	static const u8 regs[2] = { S2MPG10_L25M_CTRL, S2MPG10_L26M_CTRL };
	static const u32 base_mv[2] = { 700, 1800 }, min_mv[2] = { 1600, 2500 },
	    max_mv[2] = { 1950, 3300 }, want_mv[2] = { 1800, 3300 };
	for (int i = 0; i < 2; i++) {
		u8 v = 0, after = 0;
		int e = acpm_pmic(regs[i], 0, 0, 0, &v);
		if (e) {
			kv2("  ACPM read error ", (u32)-e, " reg ", regs[i]);
			return 0;
		}
		u32 mv = base_mv[i] + 25 * (v & 0x3f);
		u8 sel = v & 0x3f;
		if (mv < min_mv[i] || mv > max_mv[i])
			sel = (u8)((want_mv[i] - base_mv[i]) / 25);
		e = acpm_pmic(regs[i], 2, (u8)(0x80 | sel), 0xbf, 0);
		if (!e)
			e = acpm_pmic(regs[i], 0, 0, 0, &after);
		if (verbose || e) {
			puts_(i ? "  L26M (TS_AVDD)  " : "  L25M (TSP_DVDD) ");
			puthex(v, 2);
			puts_(" -> ");
			puthex(after, 2);
			puts_(" = ");
			puthex(base_mv[i] + 25 * (after & 0x3f), 4);
			puts_(" mV");
			newline();
		}
		if (e) {
			kv("  ACPM update error ", (u32)-e);
			return 0;
		}
	}
	return 1;
}

/* ---- touch probe (ST FTM5, bit-banged SPI mode 0, see board.h) ---- */

static void gpio_func(u64 bank, int pin, u32 fn)
{
	u32 con = rd32(bank);
	wr32(bank, (con & ~(0xfu << (4 * pin))) | (fn << (4 * pin)));
}

static void gpio_set(u64 bank, int pin, int v)
{
	u32 d = rd32(bank + 4);
	wr32(bank + 4, v ? d | (1u << pin) : d & ~(1u << pin));
}

static void spi_delay(void)
{
	u64 t0 = now();
	while (now() - t0 < timer_hz / 1000000 + 1)	/* ~1 us: < 500 kHz */
		;
}

static u8 spi_byte(u8 out)
{
	u8 in = 0;
	for (int b = 7; b >= 0; b--) {
		gpio_set(TS_GPP23, 1, (out >> b) & 1);
		spi_delay();
		gpio_set(TS_GPP23, 0, 1);
		in = (u8)(in << 1 | ((rd32(TS_GPP23 + 4) >> 2) & 1));
		spi_delay();
		gpio_set(TS_GPP23, 0, 0);
	}
	return in;
}

/* CS low, tx[ntx] out, then nrx bytes in (with 0x00 out), CS high */
static void spi_xfer(const u8 *tx, int ntx, u8 *rx, int nrx)
{
	gpio_set(TS_GPP23, 3, 0);
	spi_delay();
	for (int i = 0; i < ntx; i++)
		spi_byte(tx[i]);
	for (int i = 0; i < nrx; i++)
		rx[i] = spi_byte(0);
	spi_delay();
	gpio_set(TS_GPP23, 3, 1);
	delay_ms(1);
}

static void ts_pins(void)
{
	gpio_func(TS_GPP26, 1, 1);		/* TS_SPI_SEL: output, low = AP */
	gpio_set(TS_GPP26, 1, 0);
	gpio_set(TS_GPP23, 3, 1);		/* CS high before it becomes an output */
	gpio_set(TS_GPP23, 0, 0);
	gpio_func(TS_GPP23, 0, 1);		/* CLK out */
	gpio_func(TS_GPP23, 1, 1);		/* MOSI out */
	gpio_func(TS_GPP23, 2, 0);		/* MISO in */
	gpio_func(TS_GPP23, 3, 1);		/* CS out */
	gpio_func(TS_GPP26, 0, 1);		/* TS_RESET_L out */
}

/* one 8-byte event from the FIFO (cmd 0x87, one dummy byte) */
static void ts_event(u8 *ev)
{
	u8 cmd = 0x87, rx[9];
	spi_xfer(&cmd, 1, rx, 9);
	for (int i = 0; i < 8; i++)
		ev[i] = rx[i + 1];
}

static void ts_show(const char *label, const u8 *b, int n)
{
	puts_(label);
	for (int i = 0; i < n; i++) {
		putch(' ');
		putch("0123456789abcdef"[b[i] >> 4]);
		putch("0123456789abcdef"[b[i] & 15]);
	}
	newline();
}

/* Returns 1 if the chip answered with the FTM5 id. */
static int ts_probe(void)
{
	kv2("  GPP23 con ", rd32(TS_GPP23), " dat ", rd32(TS_GPP23 + 4));
	kv2("  GPP26 con ", rd32(TS_GPP26), " dat ", rd32(TS_GPP26 + 4));
	kv("  GPA9 dat (int, low = event) ", rd32(TS_GPA9_DAT));
	ts_pins();
	gpio_set(TS_GPP26, 0, 0);		/* reset pulse */
	delay_ms(10);
	gpio_set(TS_GPP26, 0, 1);
	delay_ms(200);
	u8 ev[8];
	ts_event(ev);
	ts_show("  first event (03 = ready):", ev, 8);
	static const u8 rd_id[5] = { 0xfb, 0x20, 0x00, 0x00, 0x00 };	/* HW reg 0x20000000 */
	u8 id[7];
	spi_xfer(rd_id, 5, id, 7);
	ts_show("  chip id (dummy, 36 48 ...):", id, 7);
	if (id[1] != 0x36 || id[2] != 0x48) {
		say(C_ERR, "  no FTM5 answer: touch power (LDO25M/26M) off?");
		return 0;
	}
	static const u8 pre[7] = { 0xfa, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 scan[3] = { 0xa0, 0x00, 0xff };	/* active scan, all sensing */
	spi_xfer(pre, 7, 0, 0);
	spi_xfer(scan, 3, 0, 0);
	say(C_OK, "  FTM5 answers; scanning on: touch the screen");
	return 1;
}

/* ---- UFS command test (read-only), to find how XNU should take the host over ---- */

#define UFS_IS		0x20
#define UFS_HCS		0x30
#define UFS_UTRLBA	0x50
#define UFS_UTRLBAU	0x54
#define UFS_DBR		0x58
#define UFS_RSR		0x60

/* UCD (cmd UPIU, response UPIU, PRDT) and a 4 KiB data buffer, in the shim image */
__asm__(".text\n.balign 4096\nufs_ucd_buf:\n.zero 8192\nufs_data_buf:\n.zero 4096\nufs_list_buf:\n.zero 4096\n");

static u64 sym_addr_ucd(void) { u64 a; __asm__("adr %0, ufs_ucd_buf" : "=r"(a)); return a; }
static u64 sym_addr_data(void) { u64 a; __asm__("adr %0, ufs_data_buf" : "=r"(a)); return a; }
static u64 sym_addr_list(void) { u64 a; __asm__("adr %0, ufs_list_buf" : "=r"(a)); return a; }

static void zero32(u64 a, u32 n)
{
	for (u32 i = 0; i < n; i += 4)
		wr32(a + i, 0);
}

static u64 ufs_ucd_override;	/* 0: the shim's buffer, else ABL's command buffer */

/*
 * One SCSI command in UTRD slot `slot` of the list at `list`, Exynos format
 * (byte offsets, 16-byte PRDT entries, 4 KiB segments). Returns OCS (0 = ok),
 * 0x100 + response status, or 0x200 on timeout. MMU off: no cache maintenance.
 */
static u32 ufs_cmd(u64 hci, u64 list, int slot, u8 lun, const u8 *cdb, int cdb_len, u64 data, u32 len)
{
	u64 ucd = ufs_ucd_override ? ufs_ucd_override : sym_addr_ucd(), utrd = list + 32 * slot;
	int write = cdb[0] == 0x2a; /* only the bounded boot-success helper uses WRITE(10) */
	zero32(ucd, 1024 + 16);
	zero32(utrd, 32);
	/* task tag = slot, as Linux does: the Exynos host matches responses by it */
	wr32(ucd + 0, (u32)0x01 | (len ? (write ? 0x20u : 0x40u) : 0) << 8 | (u32)lun << 16 | (u32)slot << 24);
	wr32(ucd + 12, __builtin_bswap32(len));
	u32 c[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < cdb_len; i++)
		c[i / 4] |= (u32)cdb[i] << (8 * (i % 4));
	for (int i = 0; i < 4; i++)
		wr32(ucd + 16 + 4 * i, c[i]);
	if (len) {
		wr32(ucd + 1024 + 0, (u32)data);
		wr32(ucd + 1024 + 4, (u32)(data >> 32));
		wr32(ucd + 1024 + 12, len - 1);
	}
	wr32(utrd + 0, (1u << 28) | ((len ? (write ? 1u : 2u) : 0u) << 25) | (1u << 24));
	wr32(utrd + 8, 0x0f);
	wr32(utrd + 16, (u32)ucd);
	wr32(utrd + 20, (u32)(ucd >> 32));
	wr32(utrd + 24, 512u << 16 | 512u);
	wr32(utrd + 28, 1024u << 16 | (len ? 16u : 0u));
	/* Acknowledge the preceding completion before every new request,
	 * including READ immediately after TUR. We poll rather than use IRQs. */
	wr32(hci + UFS_IS, rd32(hci + UFS_IS));
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(hci + UFS_DBR, 1u << slot);
	u64 t0 = now();
	while (rd32(hci + UFS_DBR) & (1u << slot))
		if (now() - t0 > timer_hz * (cdb[0] == 0x1b ? 10u : 1u))
			return 0x200;
	__asm__ volatile("dsb sy" ::: "memory");
	u32 ocs = rd32(utrd + 8) & 0xff;
	u32 r0 = rd32(ucd + 512);
	u32 r1 = rd32(ucd + 516);
	/* GS101's BROKEN_OCS_FATAL_ERROR quirk: a matching error response
	 * remains a SCSI error, not an unqualified controller failure. */
	int matching = (r0 & 0xff) == 0x21 && ((r0 >> 16) & 0xff) == lun &&
	    (r0 >> 24) == (u32)slot;
	if (ocs && !(ocs == 7 && matching && (r1 & 0xffff0000u)))
		return ocs;
	if (!matching)
		return 0x300 | (r0 & 0xff);
	if ((r0 & 0xff) != 0x21)			/* RESPONSE UPIU */
		return 0x300 | (r0 & 0xff);
	return (r1 >> 16) & 0xffff ? 0x100 | ((r1 >> 24) & 0xff) : 0;
}

#define UFS_UTRLCLR	0x5c
#define UFS_UICCMD	0x90
#define UFS_UICARG1	0x94
#define UFS_UICARG2	0x98
#define UFS_UICARG3	0x9c

/* Clear a pending slot (Exynos UTRLCLR clears with 1 bits); 1 if it cleared. */
static int ufs_clear_slot(u64 hci, int slot)
{
	if (!(rd32(hci + UFS_DBR) & (1u << slot)))
		return 1;
	wr32(hci + UFS_UTRLCLR, 1u << slot);
	for (int i = 0; i < 1000 && (rd32(hci + UFS_DBR) & (1u << slot)); i++)
		delay_ms(1);
	return !(rd32(hci + UFS_DBR) & (1u << slot));
}

static u32 ufs_tur(u64 hci, u64 list, int slot)
{
	static const u8 tur[6] = { 0 };
	u32 e = 0x200;
	for (int i = 0; i < 3; i++) {
		if (!ufs_clear_slot(hci, slot))
			return 0x400;			/* doorbell stuck even after UTRLCLR */
		if (!(e = ufs_cmd(hci, list, slot, 0, tur, 6, 0, 0)))
			break;
	}
	return e;
}

static void ufs_show(const char *label, u64 hci, u32 e)
{
	puts_(label);
	puts_(" TUR ");
	puthex(e, 3);
	puts_(e == 0 ? " OK" : e == 0x200 ? " timeout" : e == 0x400 ? " slot stuck" : " error");
	newline();
	kv2("     IS ", rd32(hci + UFS_IS), " HCS ", rd32(hci + UFS_HCS));
	wr32(hci + UFS_IS, rd32(hci + UFS_IS));
}

/* UIC command; returns ARG2 result code (0 ok) or 0x100 on timeout */
static u32 ufs_uic(u64 hci, u32 cmd, u32 arg1, u32 *arg3)
{
	for (int i = 0; i < 100 && !(rd32(hci + UFS_HCS) & 8); i++)	/* UCRDY */
		delay_ms(1);
	wr32(hci + UFS_IS, 1u << 10);					/* UCCS */
	wr32(hci + UFS_UICARG1, arg1);
	wr32(hci + UFS_UICARG2, 0);
	wr32(hci + UFS_UICARG3, 0);
	wr32(hci + UFS_UICCMD, cmd);
	u64 t0 = now();
	while (!(rd32(hci + UFS_IS) & (1u << 10)))
		if (now() - t0 > timer_hz / 2)
			return 0x100;
	if (arg3)
		*arg3 = rd32(hci + UFS_UICARG3);
	return rd32(hci + UFS_UICARG2) & 0xff;
}

static void prepare_ufs(u64 dtb, int test)
{
	struct fdt f;
	u64 hci = 0, hs, vs = 0, vss;
	fdt_begin(&f, dtb);
	while (fdt_next(&f))
		if (prop_is(&f, "compatible") && contains((const char *)f.val, f.len, "ufs") &&
		    (contains((const char *)f.val, f.len, "exynos") || contains((const char *)f.val, f.len, "gs101")) &&
		    node_reg(&f, 0, &hci, &hs)) {
			node_reg(&f, 1, &vs, &vss);
			break;
		}
	if (!hci || !vs) {
		say(C_DIM, "  no Exynos UFS node: skipped");
		return;
	}
	u64 list = rd32(hci + UFS_UTRLBA) | (u64)rd32(hci + UFS_UTRLBAU) << 32;
	/* No interrupt aggregation is needed for one polled slot. */
	kv("  ABL interrupt aggregation ", rd32(hci + 0x4c));
	wr32(hci + 0x4c, 0);
	/* E1: where ABL left things */
	kv2("  E1 CLKSTOP ", rd32(vs + 0xb0), " MISC ", rd32(vs + 0xb4));
	kv2("     IS ", rd32(hci + UFS_IS), " HCS ", rd32(hci + UFS_HCS));
	kv2("     list ", list, " RSR ", rd32(hci + UFS_RSR));
	u32 pm = 0;
	u32 r = ufs_uic(hci, 0x01, 0x1571u << 16, &pm);		/* DME_GET PA_PWRMODE */
	kv2("     DME_GET PA_PWRMODE result ", r, " value ", pm);
	/* Linux order: ungate clocks, then DME_HIBERNATE_EXIT (ABL leaves the link in H8) */
	wr32(vs + 0xb0, rd32(vs + 0xb0) & ~0x1fu);
	wr32(vs + 0xb4, rd32(vs + 0xb4) & ~0xe0u);
	r = ufs_uic(hci, 0x18, 0, 0);
	delay_ms(5);
	kv2("  E2 H8 exit result ", r, " HCS ", rd32(hci + UFS_HCS));
	kv("     IS ", rd32(hci + UFS_IS));
	wr32(hci + UFS_IS, rd32(hci + UFS_IS));
	/* Resume the device itself as Linux ufshcd_set_dev_pwr_mode does.
	 * UPIU WLUN 0xd0 is the device WLUN; ACTIVE is power condition 1.
	 * These commands do not write logical blocks. */
	static const u8 sense[6] = { 0x03, 0, 0, 0, 18, 0 };
	static const u8 active[6] = { 0x1b, 0, 0, 0, 0x10, 0 };
	if (!ufs_clear_slot(hci, 1)) {
		say(C_WARN, "  pending slot: wake skipped");
		return;
	}
	u32 wake = ufs_cmd(hci, list, 1, 0xd0, sense, 6, sym_addr_data(), 18);
	kv("  DEVICE request sense ", wake);
	if (!ufs_clear_slot(hci, 1))
		return;
	wr32(hci + UFS_IS, rd32(hci + UFS_IS));
	wake = ufs_cmd(hci, list, 1, 0xd0, active, 6, 0, 0);
	kv("  DEVICE ACTIVE ", wake);
	if (!ufs_clear_slot(hci, 1))
		return;
	delay_ms(100);
	if (!test)
		return;
	static const u8 tur[6] = { 0 };
	static const u8 rd10[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };
	for (u8 lun = 0; lun < 8; lun++) {
		/* Never reuse the shared command buffer while DMA may be pending. */
		if (!ufs_clear_slot(hci, 1)) {
			say(C_WARN, "  pending slot cannot clear: scan stopped");
			break;
		}
		wr32(hci + UFS_IS, rd32(hci + UFS_IS));
		u32 e = ufs_cmd(hci, list, 1, lun, tur, 6, 0, 0);
		u64 resp = sym_addr_ucd() + 512;
		puts_("  LUN "); puthex(lun, 1);
		puts_(" TUR "); puthex(e, 3);
		puts_(" OCS "); puthex(rd32(list + 32 + 8) & 0xff, 2);
		puts_(" DBR "); puthex(rd32(hci + UFS_DBR), 8); newline();
		/* Fixed-format sense: key at byte 2, ASC/ASCQ at bytes 12/13. */
		u32 sk = rd32(resp + 36) & 0xf;
		u32 asc = rd32(resp + 44) >> 16;
		puts_("    rsp1 "); puthex(rd32(resp + 4), 8);
		puts_(" key "); puthex(sk, 1);
		puts_(" ASC/ASCQ (LE) "); puthex(asc, 4); newline();
		if (e != 0)
			continue;
		u64 data = sym_addr_data();
		zero32(data, 4096);
		e = ufs_cmd(hci, list, 1, lun, rd10, 10, data, 4096);
		kv2("    READ LBA1 ", e, " first word ", rd32(data));
		kv2("    second word ", rd32(data + 4), " DBR ", rd32(hci + UFS_DBR));
		kv2("    read OCS ", rd32(list + 40), " response ", rd32(resp));
		kv2("    read rsp1 ", rd32(resp + 4), " IS ", rd32(hci + UFS_IS));
		int gpt = e == 0 && rd32(data) == 0x20494645 && rd32(data + 4) == 0x54524150;
		say(gpt ? C_OK : C_WARN, gpt ? "  EFI PART: storage works" : "  no GPT header");
		/* Stop on a read timeout so subsequent commands do not obscure it. */
		if (gpt || e == 0x200)
			break;
	}
	/* Quiesce the final timed-out command, too. */
	if (!ufs_clear_slot(hci, 1))
		say(C_WARN, "  final slot remains pending");

}

#include "log-viewer.h"
#include "mark-success.h"
#include "power-usb.h"

static void __attribute__((noreturn)) diag(u64 dtb)
{
	u64 v;
	u32 len;
	if (chosen_prop(dtb, "bootargs", &v, &len) && contains((const char *)v, len, "shimpowerusb"))
		diag_power_usb(dtb);
	if (chosen_prop(dtb, "bootargs", &v, &len) && contains((const char *)v, len, "shimmarka"))
		diag_mark_a(dtb);
	if (chosen_prop(dtb, "bootargs", &v, &len) && contains((const char *)v, len, "shimslot"))
		diag_slots(dtb);
	if (chosen_prop(dtb, "bootargs", &v, &len) && contains((const char *)v, len, "shimlog"))
		diag_log(dtb);
	say(C_OK, "DIAGNOSTIC BOOT (docs/bringup.md)");
	kv2("PSCI ", smc(0x84000000, 0, 0, 0), " SMCCC ", smc(0x80000000, 0, 0, 0));
	if (chosen_prop(dtb, "bootargs", &v, &len)) {
		puts_("bootargs: ");
		for (u32 i = 0; i < len && i < 400 && *(volatile char *)(v + i); i++)
			putch(*(volatile char *)(v + i));
		newline();
	}
	say(C_DIM, "memory (XNU wants the range below):");
	kv2("  XNU ", BLUEJAY_PHYS_BASE, " size ", BLUEJAY_MEM_SIZE);
	say(check_memory(dtb, BLUEJAY_PHYS_BASE, BLUEJAY_MEM_SIZE, 1) ? C_OK : C_ERR, "  -> memory check done");
	say(C_DIM, "framebuffer:");
	fdt_framebuffer(dtb, 1);
	decon_refresh(dtb, -1);
	say(C_DIM, "watchdogs:");
	watchdogs_off(dtb, 1);
	say(C_DIM, "UFS (ABL must leave the link up):");
	diag_ufs(dtb);
	say(C_DIM, "UFS test (wake + reads; no block writes):");
	prepare_ufs(dtb, 1);
	say(C_DIM, "touch power (PMIC LDO25M/26M via ACPM):");
	if (!fdt_has_decon(dtb))
		say(C_DIM, "  not a GS101 (no DECON in the tree): skipped");
	else if (touch_power(1))
		delay_ms(20);
	say(C_DIM, "touch (ST FTM5 on spi_11, bit-banged):");
	int touch_ok = ts_probe();
	say(C_DIM, "keys (DAT registers, press the buttons):");
	int keys_row = row, kind = uart_kind;
	u8 last_touch[8] = { 0 };
	u32 last[3] = { 1, 1, 1 };
	for (u64 n = 0;; n++) {
		row = keys_row;
		col = 0;
		/* serial gets a new block only when a key register changed */
		int changed = n == 0;
		for (int k = 0; k < 3; k++)
			changed |= rd32(key_dat(k)) != last[k];
		uart_kind = changed ? kind : UART_NONE;
		for (int k = 0; k < 3; k++) {
			u32 d = rd32(key_dat(k));
			last[k] = d;
			if (k == 0)
				puts_("  power ");
			else if (k == 1)
				puts_("  vol+  ");
			else
				puts_("  vol-  ");
			puthex(d, 8);
			if ((d >> key_bit(k)) & 1)
				puts_("  up  ");
			else
				puts_("  DOWN");
			newline();
		}
		if (touch_ok) {
			u8 ev[8];
			ts_event(ev);
			/* 13 enter, 23 motion, 33 leave: x = 12 bits of b2/b3, y = b4/b3 */
			if (ev[0] == 0x13 || ev[0] == 0x23 || ev[0] == 0x33)
				for (int i = 0; i < 8; i++)
					last_touch[i] = ev[i];
			ts_show("  touch event:", last_touch, 8);
			kv2("  touch x ", (u32)((last_touch[3] & 0x0f) << 8 | last_touch[2]),
			    " y ", (u32)(last_touch[4] << 4 | last_touch[3] >> 4));
		}
		puts_("  alive ");
		puthex(n, 8);
		newline();
		delay_ms(100);
	}
}

/* ------------------------------------------------------------------ boot */

static void copy64(u64 dst, u64 src, u64 n)
{
	volatile u64 *d = (volatile u64 *)dst;
	const volatile u64 *s = (const volatile u64 *)src;
	for (u64 i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(u64 dst, u64 n)
{
	volatile u64 *d = (volatile u64 *)dst;
	for (u64 i = 0; i < n / 8; i++)
		d[i] = 0;
}

static u64 image_end(u64 base)
{
	return base + *(volatile u64 *)(base + 0x10);
}

__attribute__((used)) void shim_main(u64 dtb, u64 base, u64 entry_el)
{
	timer_hz = rsr("cntfrq_el0");
	if (!timer_hz)
		timer_hz = BLUEJAY_TIMEBASE;
	int have_fdt = fdt_ok(dtb);
	u64 psize = 0;
	const char *where = "";
	u64 pa = find_payload(dtb, base, &psize, &where);
	const struct payload *p = pa ? (const struct payload *)pa : 0;
	if (p && (p->version != 1 || (p->adt_off & 7)))
		p = 0;
	if (have_fdt)
		uart_setup(dtb, p);
	/* Draw where the live tree says the panel scans out, so a mismatch is visible. */
	u64 fdt_fb = have_fdt ? fdt_framebuffer(dtb, 0) : 0;
	if (fdt_fb)
		fb_base = fdt_fb;
	fill_rows(0, FB_H, C_BG);
	row = 1;

	say(C_OK, "iOS 16 for Pixel 6a: bluejay shim");
	kv2("entry EL ", entry_el, "  now EL ", (rsr("CurrentEL") >> 2) & 3);
	kv2("MIDR ", rsr("midr_el1"), " MPIDR ", rsr("mpidr_el1"));
	kv2("CNTFRQ ", rsr("cntfrq_el0"), " using ", timer_hz);
	kv2("image ", base, " - ", image_end(base));
	kv("FDT ", dtb);
	if (!have_fdt)
		halt("no flattened device tree in x0");
	/* Make the panel show what we draw (command-mode DECON, see board.h). */
	if (!payload_flag(p, "shimdecon=0"))
		decon_refresh(dtb, 1);
	if (uart_kind == UART_PL011)
		kv("UART pl011 ", uart_base);
	else if (uart_kind == UART_SAMSUNG)
		kv("UART samsung ", uart_base);
	if (((rsr("id_aa64mmfr0_el1") >> 20) & 0xf) == 0)
		halt("this core has no 16K granule");

	u64 v;
	u32 len;
	int want_diag = !p || payload_flag(p, "shimdiag") ||
	    (chosen_prop(dtb, "bootargs", &v, &len) && contains((const char *)v, len, "shimdiag"));
	if (want_diag) {
		if (!p)
			say(C_WARN, "no SWEETXNU payload");
		diag(dtb);
	}

	kv2("payload ", pa, " size ", psize);
	puts_("  ");
	say(C_DIM, where);
	if (fdt_fb && fdt_fb != FB_BASE) {
		kv2("live framebuffer ", fdt_fb, " kernel expects ", FB_BASE);
		halt("rebuild the kernel and ADT for this framebuffer");
	}
	if (!payload_flag(p, "shimwdt=0"))
		watchdogs_off(dtb, 0);
	/* Touch supplies on before XNU (bsd/dev/bluejay_touch.c resets the chip). */
	if (fdt_has_decon(dtb) && !payload_flag(p, "shimtouchpwr=0"))
		touch_power(1);
	if (fdt_has_decon(dtb) && payload_flag(p, "shimcpu=1"))
		cpu_clocks(1, 1);
	else if (fdt_has_decon(dtb) && payload_flag(p, "shimcpu=2"))
		cpu_clocks(2, 1);
	/* ABL leaves both the UFS link and device asleep. Resume them using
	 * the same sequence verified by the diagnostic's GPT read. */
	if (fdt_has_decon(dtb) && !payload_flag(p, "shimufs=0"))
		prepare_ufs(dtb, 0);

	u64 pa_of_va = p->phys_base - p->virt_base;
	kv2("XNU RAM ", p->phys_base, " size ", p->mem_size);
	if (!check_memory(dtb, p->phys_base, p->mem_size, 0))
		halt("refusing to boot: XNU RAM is not free");
	if (overlaps(pa, psize, p->phys_base, p->mem_size) ||
	    overlaps(base, image_end(base) - base, p->phys_base, p->mem_size) ||
	    overlaps(dtb, be32(dtb + 4), p->phys_base, p->mem_size))
		halt("payload, shim or FDT inside XNU RAM");

	for (u32 i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		u64 dst = s->vmaddr + pa_of_va;
		if (dst < p->phys_base || dst + s->vmsize > p->phys_base + p->mem_size)
			halt("segment outside XNU RAM");
		copy64(dst, pa + s->file_off, s->file_size);
		if (s->vmsize > s->file_size) {
			u64 z = (s->file_size + 7) & ~7UL;
			zero64(dst + z, s->vmsize - z);
		}
	}
	kv2("segments ", p->nsegs, " end ", p->kernel_end + pa_of_va);

	u64 adt_pa = p->kernel_end + pa_of_va;
	copy64(adt_pa, pa + p->adt_off, p->adt_size);
	u64 ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	u64 top = ba_pa + PAGE_16K;
	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->revision = 2;
	ba->version = 2;
	ba->virt_base = p->virt_base;
	ba->phys_base = p->phys_base;
	ba->mem_size = p->mem_size;
	ba->top_of_kernel_data = top;
	ba->v_base = FB_BASE;
	ba->v_display = 1;
	ba->v_row_bytes = FB_STRIDE;
	ba->v_width = FB_W;
	ba->v_height = FB_H;
	ba->v_depth = 32;
	ba->device_tree = adt_pa - pa_of_va;		/* XNU reads it with the MMU on: a VA */
	ba->device_tree_length = (u32)p->adt_size;
	int cl = 0;
	for (; cl < BOOT_LINE_LENGTH - 1 && p->cmdline[cl]; cl++)
		ba->command_line[cl] = p->cmdline[cl];
	if (uart_kind && !payload_flag(p, "sweetuart=") && cl + 12 < BOOT_LINE_LENGTH) {
		const char *k = " sweetuart=1";
		for (int i = 0; k[i]; i++)
			ba->command_line[cl++] = k[i];
	}
	ba->command_line[cl] = 0;
	ba->mem_size_actual = p->mem_size;
	kv2("boot_args ", ba_pa, " top ", top);

	u64 entry = p->entry_va + pa_of_va;
	kv("jumping to XNU at ", entry);
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	register u64 x0 __asm__("x0") = ba_pa;
	__asm__ volatile("mov x1, xzr; mov x2, xzr; mov x3, xzr; br %1" : : "r"(x0), "r"(entry) : "x1", "x2", "x3", "memory");
	halt("XNU returned");
}
