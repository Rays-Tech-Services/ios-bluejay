/* Read-only on-phone viewer for the kernel's persistent SWEETLOG ring.
 * Boot a payload-free shim with boot-image cmdline "shimlog". No kernel
 * runs, so the prior iOS boot's log is not overwritten. */
static u64 log_hci, log_list, log_base, log_total;
static u64 log_userdata_first;
static u32 log_size;
static u32 log_dev_magic, log_dev_version, log_slot_a, log_slot_b;
static u8 log_lun;
static u64 log_boot_a[4], log_boot_b[4];
static u32 log_gpt[4], log_devs[4];
static u64 log_dev_lba[4];
static u32 log_dev_ver[4], log_dev_a[4], log_dev_b[4];
static char log_selected[8192];
static u64 log_selected_total;

static int log_name(u64 entry, const char *name)
{
	for (int j = 0; j < 36; j++) {
		volatile u8 *p = (volatile u8 *)(entry + 56 + 2 * j);
		if (p[0] != (u8)name[j] || p[1])
			return 0;
		if (!name[j])
			return 1;
	}
	return 0;
}

static u64 log_le64(u64 p)
{
	return rd32(p) | (u64)rd32(p + 4) << 32;
}

static int log_read(u64 lba)
{
	if (lba > 0xffffffffu || !ufs_clear_slot(log_hci, 1))
		return 0;
	u8 cdb[10] = { 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, 0, 1, 0 };
	u32 e = ufs_cmd(log_hci, log_list, 1, log_lun, cdb, 10, sym_addr_data(), 4096);
	if (e) {
		kv2("log read failed at LBA ", lba, " result ", e);
		ufs_clear_slot(log_hci, 1);
	}
	return e == 0;
}

static int log_open(u64 dtb)
{
	struct fdt f;
	u64 hs;
	fdt_begin(&f, dtb);
	while (fdt_next(&f))
		if (prop_is(&f, "compatible") && contains((const char *)f.val, f.len, "ufs") &&
		    (contains((const char *)f.val, f.len, "exynos") || contains((const char *)f.val, f.len, "gs101")) &&
		    node_reg(&f, 0, &log_hci, &hs))
			break;
	if (!log_hci)
		return 0;
	prepare_ufs(dtb, 0);
	log_list = rd32(log_hci + UFS_UTRLBA) | (u64)rd32(log_hci + UFS_UTRLBAU) << 32;
	u64 data = sym_addr_data();
	if (!log_read(1) || rd32(data) != 0x20494645 || rd32(data + 4) != 0x54524150)
		return 0;
	u64 entries = log_le64(data + 72), last = 0, devinfo = 0;
	u32 count = rd32(data + 80), esz = rd32(data + 84);
	if (esz != 128 || !count || count > 4096)
		return 0;
	for (u32 i = 0; i < count; i++) {
		if (!(i % 32) && !log_read(entries + i / 32))
			return 0;
		u64 e = data + (i % 32) * 128;
		if (log_name(e, "userdata") && log_le64(e + 40) > log_le64(e + 32) + 1024) {
			last = log_le64(e + 40);
			log_userdata_first = log_le64(e + 32);
		}
		if (log_name(e, "devinfo") && log_le64(e + 40) >= log_le64(e + 32))
			devinfo = log_le64(e + 32);
	}
	if (devinfo && log_read(devinfo)) {
		log_dev_magic = rd32(data);
		log_dev_version = rd32(data + 4);
		log_slot_a = rd32(data + 48);
		log_slot_b = rd32(data + 52);
	}
	if (!last)
		return 0;
	log_base = last - 1024;
	if (!log_read(log_base) || rd32(data) != 0x45455753 || rd32(data + 4) != 0x474f4c54 ||
	    rd32(data + 8) != 1 || rd32(data + 12) != (4u << 20))
		return 0;
	log_size = rd32(data + 12);
	log_total = log_le64(data + 16);
	return log_total != 0;
}

/* Inspect all four responding LUNs; boot metadata need not live with userdata. */
static void log_probe_slots(void)
{
	u64 data = sym_addr_data();
	for (log_lun = 0; log_lun < 4; log_lun++) {
		if (!log_read(1))
			continue;
		log_gpt[log_lun] = rd32(data);
		if (rd32(data) != 0x20494645 || rd32(data + 4) != 0x54524150)
			continue;
		u64 entries = log_le64(data + 72), dev = 0;
		u32 count = rd32(data + 80), esz = rd32(data + 84);
		if (esz != 128 || count > 4096)
			continue;
		for (u32 i = 0; i < count; i++) {
			if (!(i % 32) && !log_read(entries + i / 32))
				break;
			u64 e = data + (i % 32) * 128;
			if (log_name(e, "boot_a"))
				log_boot_a[log_lun] = log_le64(e + 48);
			if (log_name(e, "boot_b"))
				log_boot_b[log_lun] = log_le64(e + 48);
			if (log_name(e, "devinfo"))
				dev = log_le64(e + 32);
		}
		if (dev && log_read(dev)) {
			log_dev_lba[log_lun] = dev;
			log_devs[log_lun] = rd32(data);
			log_dev_ver[log_lun] = rd32(data + 4);
			log_dev_a[log_lun] = rd32(data + 48);
			log_dev_b[log_lun] = rd32(data + 52);
			if (rd32(data) == 0x49564544) {
				log_dev_magic = rd32(data);
				log_dev_version = rd32(data + 4);
				log_slot_a = rd32(data + 48);
				log_slot_b = rd32(data + 52);
			}
		}
	}
	log_lun = 0;
}

static void __attribute__((noreturn)) diag_slots(u64 dtb)
{
	watchdogs_off(dtb, 0);
	if (!log_open(dtb)) {
		say(C_ERR, "Cannot open UFS metadata (nothing written)");
		for (;;)
			__asm__ volatile("wfe");
	}
	log_probe_slots();
	fill_rows(0, FB_H, C_BG);
	row = col = 0;
	say(C_OK, "BOOT SLOT METADATA - READ ONLY");
	say(C_DIM, "No graphics changes; no storage writes");
	for (int i = 0; i < 4; i++) {
		kv2("LUN ", i, " GPT signature ", log_gpt[i]);
		kv2(" boot A attrs ", log_boot_a[i], " B attrs ", log_boot_b[i]);
		kv2(" devinfo LBA ", log_dev_lba[i], " magic ", log_devs[i]);
		kv(" devinfo version raw ", log_dev_ver[i]);
		kv2(" slot A word ", log_dev_a[i], " B word ", log_dev_b[i]);
		newline();
	}
	say(C_OK, "Read-only inspection complete");
	for (;;)
		__asm__ volatile("wfe");
}

static void log_filter(void)
{
	char line[512];
	u32 n = 0;
	u64 first = log_total > log_size ? log_total - log_size : 0, cached = ~(u64)0;
	for (u64 p = first; p < log_total; p++) {
		u64 off = p % log_size, block = off / 4096;
		if (block != cached) {
			if (!log_read(log_base + 1 + block))
				break;
			cached = block;
		}
		u8 c = *(volatile u8 *)(sym_addr_data() + off % 4096);
		if (c == '\n') {
			if (contains(line, n, "SWEET SURF") || contains(line, n, "SWEET FB") ||
			    contains(line, n, "CRASH") || contains(line, n, "wallpaper") ||
			    contains(line, n, "Wallpaper") || contains(line, n, "cfpref") ||
			    contains(line, n, "CFPref") || contains(line, n, "failed")) {
				for (u32 j = 0; j < n; j++)
					log_selected[log_selected_total++ % sizeof(log_selected)] = line[j];
				log_selected[log_selected_total++ % sizeof(log_selected)] = '\n';
			}
			n = 0;
		} else if (c != '\r' && c && n < sizeof(line) - 1) {
			line[n++] = c == '\t' ? ' ' : c;
		}
	}
}

static u64 log_page(u64 start, int filtered)
{
	fill_rows(0, FB_H, C_BG);
	row = col = 0;
	color = 0xffff0000; puts_("RED ");
	color = 0xff00ff00; puts_("GREEN ");
	color = 0xff0000ff; puts_("BLUE ");
	color = C_FG; puts_("WHITE"); newline();
	say(C_OK, "SAVED iOS LOG - read-only");
	kv2("devinfo magic ", log_dev_magic, " version ", log_dev_version);
	kv2("slot A raw ", log_slot_a, " slot B raw ", log_slot_b);
	for (int i = 0; i < 4; i++) {
		puts_("LUN "); puthex(i, 1);
		puts_(" GPT "); puthex(log_gpt[i], 8);
		puts_(" dev "); puthex(log_devs[i], 8); newline();
		if (log_boot_a[i] || log_boot_b[i])
			kv2("  boot A attrs ", log_boot_a[i], " B ", log_boot_b[i]);
	}
	kv2("position ", start, " total ", log_total);
	say(C_DIM, filtered ? "GRAPHICS: Vol+/- pages; Power full log" : "FULL LOG: Vol+/- pages; Power graphics");
	if (filtered) {
		u64 p = start;
		for (; p < log_selected_total && row < FB_H / CELL_H - 2; p++)
			putch(log_selected[p % sizeof(log_selected)]);
		return p;
	}
	u64 cached = ~(u64)0, p = start;
	for (; p < log_total && row < FB_H / CELL_H - 2; p++) {
		u64 off = p % log_size, block = off / 4096;
		if (block != cached) {
			if (!log_read(log_base + 1 + block))
				break;
			cached = block;
		}
		u8 c = *(volatile u8 *)(sym_addr_data() + off % 4096);
		if (c == '\r' || c == 0)
			continue;
		if (c == '\t')
			c = ' ';
		putch(c == '\n' || (c >= 32 && c <= 126) ? (char)c : '?');
	}
	return p;
}

static void __attribute__((noreturn)) diag_log(u64 dtb)
{
	watchdogs_off(dtb, 0);
	if (!log_open(dtb)) {
		say(C_ERR, "No readable saved iOS log (nothing written)");
		for (;;)
			__asm__ volatile("wfe");
	}
	log_probe_slots();
	log_filter();
	u64 first = log_total > log_size ? log_total - log_size : 0;
	u64 tail = log_total - first > 3000 ? log_total - 3000 : first;
	u64 selected_first = log_selected_total > sizeof(log_selected) ? log_selected_total - sizeof(log_selected) : 0;
	int filtered = log_selected_total != 0;
	u64 start = filtered ? selected_first : tail, end = log_page(start, filtered);
	u32 old = 0;
	for (;;) {
		u32 keys = 0;
		for (int k = 0; k < 3; k++)
			if (!((rd32(key_dat(k)) >> key_bit(k)) & 1))
				keys |= 1u << k;
		u32 pressed = keys & ~old;
		old = keys;
		if (pressed) {
			u64 low = filtered ? selected_first : first;
			u64 high = filtered ? log_selected_total : log_total;
			if (pressed & 1) {
				filtered = !filtered;
				start = filtered ? selected_first : tail;
			}
			else if (pressed & 2)
				start = start - low > 3000 ? start - 3000 : low;
			else if ((pressed & 4) && end < high)
				start = end;
			end = log_page(start, filtered);
		}
		delay_ms(100);
	}
}
