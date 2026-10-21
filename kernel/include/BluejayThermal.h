/* CPU clock policy from the battery thermistor, shared with host checks. */
#ifndef BLUEJAY_THERMAL_H
#define BLUEJAY_THERMAL_H

/* ACPM DVFS domains (mainline google,gs101-acpm.h). */
#define BLUEJAY_DVFS_CPUCL1     3       /* 2 x Cortex-A76 */
#define BLUEJAY_DVFS_CPUCL2     4       /* 2 x Cortex-X1 */

struct BluejayThermalLevel {
	unsigned a76_khz, x1_khz;
};

/* Level 0 = full speed (X1 at the boot-arg maximum), 1 = warm, 2 = hot. */
static inline BluejayThermalLevel
bluejay_thermal_clocks(unsigned level, unsigned x1_max_khz)
{
	BluejayThermalLevel l;
	l.a76_khz = level >= 2 ? 1803000u : 2253000u;
	l.x1_khz = level >= 2 ? 1803000u : level == 1 ? 2253000u : x1_max_khz;
	if (l.x1_khz > x1_max_khz) {
		l.x1_khz = x1_max_khz;
	}
	return l;
}

/*
 * Battery temperature in 0.01 C. Step up at 40 / 44 C, back down only below
 * 42 / 38 C so the clocks do not oscillate around one threshold. An invalid
 * reading (outside -20..80 C) is treated as hot.
 */
static inline unsigned
bluejay_thermal_next(unsigned level, int centi_c)
{
	if (centi_c < -2000 || centi_c > 8000 || centi_c >= 4400) {
		return 2;
	}
	if (centi_c >= 4000) {
		return level >= 2 && centi_c >= 4200 ? 2 : 1;
	}
	if (level >= 2) {
		return centi_c >= 4200 ? 2 : 1;
	}
	if (level == 1) {
		return centi_c >= 3800 ? 1 : 0;
	}
	return 0;
}
#endif
