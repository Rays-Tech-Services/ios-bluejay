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

/*
 * Touch: ST FTM5 (driver "stm_fts", fw ftm5_fw_b3) on spi_11, from Google's
 * gs101-common-touch.dtsi / gs101-fts-touch.dtsi; bank offsets from mainline
 * pinctrl-exynos-arm64.c (gs101_pin_peric1 / gs101_pin_alive).
 *   CLK=GPP23[0] MOSI=GPP23[1] MISO=GPP23[2] CS=GPP23[3]
 *   TS_RESET_L=GPP26[0]  TS_SPI_SEL=GPP26[1] (low: AP owns the bus, high: AOC)
 *   TS_INT_L=GPA9[0]
 * The shim and the kernel bit-bang the bus on these GPIOs (no USI/clock setup).
 */
#define PERIC1_PINCTRL		0x10C40000UL
#define TS_GPP23		(PERIC1_PINCTRL + 0x60)	/* CON +0, DAT +4, PUD +8 */
#define TS_GPP26		(PERIC1_PINCTRL + 0xc0)
#define TS_GPA9_DAT		(0x174d0000UL + 0xc0 + 4)

/*
 * Display: DECON0 main registers (gs101-drm-dpu.dtsi), from Google's
 * display/samsung/cal_9845/regs-decon.h. In command mode with hardware
 * (TE) triggering, Linux unmasks the trigger per frame and masks it again
 * afterwards; left masked, the panel never shows new framebuffer contents.
 */
#define DECON0			0x1C300000UL
#define DECON_GLOBAL_CON	0x20		/* bit 8: command mode, bits 0/1: enable */
#define DECON_TRIG_CON		0x30		/* bit 0 HW_TRIG_EN, 4 HW_TRIG_MASK_DECON, 8 SW_TRIG_EN */
#define DECON_SHD_REG_UP_REQ	0x50		/* bit 31: global shadow update */

/*
 * Touch supplies on the main PMIC (S2MPG10), reached through the APM's ACPM
 * firmware (mainline drivers/firmware/samsung/exynos-acpm*.c, sec-acpm.c):
 * channel table in APM SRAM at 0x2039000 + 0xa000, PMIC requests on ACPM
 * channel 2 / speedy channel 0, doorbell = AP2APM mailbox INTGR1 bit 2.
 * L25M_CTRL 0x5c "L25M_TSP_DVDD": 0.7 V + 25 mV * sel[5:0], rail 1.6-1.95 V
 * L26M_CTRL 0x5d "L26M_TS_AVDD":  1.8 V + 25 mV * sel[5:0], rail 2.5-3.3 V
 * bit 7 = enable (Google gs101-pmic.dtsi names, mainline s2mps11.c ranges).
 */
#define APM_SRAM		0x02039000UL
#define ACPM_INITDATA		0xa000
#define AP2APM_MBOX		0x17610000UL
#define MBOX_INTGR1		0x40
#define ACPM_PMIC_CHAN		2
#define ACPM_DVFS_CHAN		0
#define ACPM_DVFS_CPUCL0	2
#define ACPM_DVFS_FREQ_REQ	0
#define ACPM_DVFS_FREQ_GET	1
#define S2MPG10_L25M_CTRL	0x5c
#define S2MPG10_L26M_CTRL	0x5d
