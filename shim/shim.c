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

static void __attribute__((noreturn)) diag(u64 dtb)
{
	u64 v;
	u32 len;
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
