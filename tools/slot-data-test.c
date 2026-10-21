#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
#include "../shim/slot-data.h"

int main(void)
{
	u32 out;
	for (u32 retry = 0; retry <= 3; retry++) {
		u32 old = 0xc00 | retry;
		assert(slot_a_success_word(0x49564544, 0x000f0003, old, 0x800, &out));
		assert(out == (old | 0x200));
		assert((out ^ old) == 0x200);
		assert(slot_a_success_word(0x49564544, 0x000f0003, out, 0x800, &out));
		assert(out == (old | 0x200));
	}
	u32 bad[][4] = {
		{0, 0x000f0003, 0xc03, 0x800},
		{0x49564544, 3, 0xc03, 0x800},
		{0x49564544, 0x000f0003, 0xc04, 0x800},
		{0x49564544, 0x000f0003, 0xd03, 0x800},
		{0x49564544, 0x000f0003, 0x803, 0x800},
		{0x49564544, 0x000f0003, 0xc03, 0xc00},
		{0x49564544, 0x000f0003, 0x10c03, 0x800},
	};
	for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
		out = 0xdeadbeef;
		assert(!slot_a_success_word(bad[i][0], bad[i][1], bad[i][2], bad[i][3], &out));
		assert(out == 0xdeadbeef);
	}
	puts("PASS: observed DevInfo state changes only slot A success bit; wrong states rejected");
}
