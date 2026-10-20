/*
 * Pixel 6a (bluejay) constants the shim needs before (or without) a payload.
 * Keep in sync with tools/mkadt.py, which is what the kernel actually uses;
 * the meaning and the confidence of each value are in docs/hardware.md.
 */
#pragma once

/* Splash framebuffer ABL leaves scanning out (a8r8g8b8) */
#define FB_BASE			0xfac00000UL
#define FB_W			1080
#define FB_H			2400
#define FB_STRIDE		(FB_W * 4)

/* XNU's RAM (mkadt.py PHYS_BASE / MEM_SIZE), for the diagnostic memory check */
#define BLUEJAY_PHYS_BASE	0x880000000UL
#define BLUEJAY_MEM_SIZE	0xE0000000UL
/* gs101.dtsi timer clock-frequency, used when CNTFRQ_EL0 reads 0 */
#define BLUEJAY_TIMEBASE	24576000UL
/* USI serial_0, on the USB-C SBU pins after "fastboot oem uart enable" */
#define SAMSUNG_UART		0x10a00000UL

/* gpio-keys DAT registers and bits (active low): power, vol+, vol- */
static inline unsigned long key_dat(int k)
{
	if (k == 0)
		return 0x174d0000UL + 0xe0 + 4;		/* gpa10 */
	if (k == 1)
		return 0x174e0000UL + 0x40 + 4;		/* gpa8 */
	return 0x174e0000UL + 0x20 + 4;			/* gpa7 */
}

static inline int key_bit(int k)
{
	return k == 0 ? 1 : k == 1 ? 1 : 3;
}
