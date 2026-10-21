/* One-time, explicit shimmarka boot only. No normal boot calls this.
 * Google gs-common bootctrl/1.2/DevInfo.h defines success as flags bit 1.
 * Metadata location/state are checked against IMG_5925 and fastboot.
 * Original 4 KiB block is saved outside HFS and outside SWEETLOG before
 * changing the single success bit. Both backup and change are read back. */
#include "slot-data.h"

static int slot_write(u8 lun, u64 lba)
{
	if (lba > 0xffffffffu || !ufs_clear_slot(log_hci, 1))
		return 0;
	u8 cdb[10] = { 0x2a, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, 0, 1, 0 };
	u32 e = ufs_cmd(log_hci, log_list, 1, lun, cdb, 10, sym_addr_data(), 4096);
	if (e) {
		kv("WRITE result ", e);
		ufs_clear_slot(log_hci, 1);
		return 0;
	}
	static const u8 sync[10] = { 0x35 };
	e = ufs_cmd(log_hci, log_list, 1, lun, sync, 10, 0, 0);
	if (e)
		kv("SYNC result ", e);
	return e == 0;
}

static int slot_same(u64 saved, u32 new_a)
{
	for (u32 i = 0; i < 4096; i += 4)
		if (rd32(sym_addr_data() + i) != (i == 48 ? new_a : rd32(saved + i)))
			return 0;
	return 1;
}

static void __attribute__((noreturn)) diag_mark_a(u64 dtb)
{
	watchdogs_off(dtb, 0);
	say(C_WARN, "ONE-TIME SLOT A SUCCESS UPDATE");
	if (!log_open(dtb))
		goto fail;
	log_probe_slots();
	if (log_dev_lba[3] != 6 || log_devs[3] != 0x49564544)
		goto fail;
	u32 new_a;
	if (!slot_a_success_word(log_devs[3], log_dev_ver[3], log_dev_a[3], log_dev_b[3], &new_a))
		goto fail;
	kv2("Slot A before ", log_dev_a[3], " requested ", new_a);
	if (new_a == log_dev_a[3]) {
		say(C_OK, "Already successful; nothing written");
		goto stop;
	}
	/* Prove the backup lies beyond the actual HFS volume, before log area. */
	log_lun = 0;
	if (!log_read(log_userdata_first))
		goto fail;
	u64 data = sym_addr_data(), vh = data + 1024;
	u16 signature = (u16)(*(volatile u8 *)vh << 8) | *(volatile u8 *)(vh + 1);
	if ((signature != 0x4858 && signature != 0x482b) || be32(vh + 40) != 4096 ||
	    !be32(vh + 44) || log_userdata_first + be32(vh + 44) >= log_base - 2)
		goto fail;
	/* Upper half of the existing 8 KiB UCD allocation is not used by commands. */
	u64 saved = sym_addr_ucd() + 4096;
	log_lun = 3;
	if (!log_read(6) || !slot_a_success_word(rd32(data), rd32(data + 4),
	    rd32(data + 48), rd32(data + 52), &new_a))
		goto fail;
	for (u32 i = 0; i < 4096; i += 4)
		wr32(saved + i, rd32(data + i));
	/* Reserve the two blocks immediately before the existing log area. */
	kv("Original block backup on userdata LBA ", log_base - 1);
	if (!slot_write(0, log_base - 1))
		goto fail;
	log_lun = 0;
	if (!log_read(log_base - 1) || !slot_same(saved, rd32(saved + 48)))
		goto fail;
	say(C_OK, "Original block backup verified");
	/* Save a small recovery descriptor in the preceding block. */
	zero32(data, 4096);
	wr32(data, 0x45444a42); wr32(data + 4, 0x4b414256); /* BJDEVBAK */
	wr32(data + 8, 1); wr32(data + 12, 3); wr32(data + 16, 6);
	wr32(data + 20, rd32(saved + 48)); wr32(data + 24, new_a);
	if (!slot_write(0, log_base - 2))
		goto fail;
	/* Recheck source hasn't changed, then modify exactly one word's one bit. */
	log_lun = 3;
	if (!log_read(6) || !slot_same(saved, rd32(saved + 48)))
		goto fail;
	wr32(data + 48, new_a);
	if (!slot_write(3, 6) || !log_read(6) || !slot_same(saved, new_a))
		goto fail;
	say(C_OK, "SLOT A SUCCESS SET AND VERIFIED");
	kv2("Old word ", rd32(saved + 48), " new word ", rd32(data + 48));
	say(C_OK, "All other bytes unchanged; flushed to storage");
	say(C_DIM, "Return to fastboot to verify slot-successful:a");
	goto stop;
fail:
	say(C_ERR, "UPDATE NOT VERIFIED - STOP (do not assume success)");
stop:
	for (;;)
		__asm__ volatile("wfe");
}
