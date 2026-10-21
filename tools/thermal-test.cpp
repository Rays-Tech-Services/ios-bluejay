#include <cassert>
#include <cstdio>
#include "../kernel/include/BluejayThermal.h"

int main()
{
	/* Steps up at thresholds. */
	assert(bluejay_thermal_next(0, 2500) == 0);
	assert(bluejay_thermal_next(0, 3999) == 0);
	assert(bluejay_thermal_next(0, 4000) == 1);
	assert(bluejay_thermal_next(0, 4400) == 2);
	assert(bluejay_thermal_next(1, 4400) == 2);
	/* Hysteresis on the way down. */
	assert(bluejay_thermal_next(2, 4300) == 2);
	assert(bluejay_thermal_next(2, 4199) == 1);
	assert(bluejay_thermal_next(2, 3700) == 1);
	assert(bluejay_thermal_next(1, 3900) == 1);
	assert(bluejay_thermal_next(1, 3799) == 0);
	/* Implausible readings are hot. */
	assert(bluejay_thermal_next(0, 8001) == 2);
	assert(bluejay_thermal_next(0, -2001) == 2);

	BluejayThermalLevel l = bluejay_thermal_clocks(0, 2802000);
	assert(l.x1_khz == 2802000 && l.a76_khz == 2253000);
	l = bluejay_thermal_clocks(1, 2802000);
	assert(l.x1_khz == 2253000 && l.a76_khz == 2253000);
	l = bluejay_thermal_clocks(2, 2802000);
	assert(l.x1_khz == 1803000 && l.a76_khz == 1803000);
	/* A lower configured maximum is never exceeded. */
	l = bluejay_thermal_clocks(0, 2253000);
	assert(l.x1_khz == 2253000);
	l = bluejay_thermal_clocks(1, 2000000);
	assert(l.x1_khz == 2000000);
	/* Neither cluster may exceed a lower boot ceiling, at any temperature. */
	for (unsigned level = 0; level < 3; ++level) {
		l = bluejay_thermal_clocks(level, 1500000, 1200000);
		assert(l.x1_khz <= 1500000 && l.a76_khz <= 1200000);
	}
	assert(bluejay_thermal_next(2, 4200) == 2);
	assert(bluejay_thermal_next(1, 3800) == 1);
	assert(bluejay_thermal_next(1, 8001) == 2);
	assert(bluejay_thermal_next(2, -2001) == 2);
	puts("thermal policy checks passed");
	return 0;
}
