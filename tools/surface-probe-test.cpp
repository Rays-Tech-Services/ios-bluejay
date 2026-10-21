#include <cassert>
#include <cstdio>
#include <vector>
#include "../kernel/include/SweetSurfaceProbe.h"

int main()
{
	constexpr unsigned w = 1125, h = 1536, stride = 4544, offset = 64;
	std::vector<unsigned char> pixels(offset + stride * h, 0);
	SweetSurfaceProbe p;
	auto sample = [&] {
		assert(sweet_surface_probe_bgra(pixels.data(), pixels.size(), offset, w, h, stride, &p));
		assert(p.samples > 4096 && p.samples < 12000);
	};
	sample(); assert(!p.nonzero && !p.rgb && !p.alpha);
	for (unsigned y = 0; y < h; y++) {
		for (unsigned x = 0; x < w; x++) {
			pixels[offset + y * stride + x * 4 + 3] = 255;
		}
	}
	sample(); assert(p.nonzero == p.samples && !p.rgb && p.alpha == p.samples);
	for (unsigned y = 0; y < h; y++) {
		for (unsigned x = 0; x < w; x++) {
			pixels[offset + y * stride + x * 4] = 255;
		}
	}
	sample(); assert(p.rgb == p.samples && p.alpha == p.samples);
	unsigned char one[4] = {1, 2, 3, 4};
	assert(sweet_surface_probe_bgra(one, 4, 0, 1, 1, 4, &p));
	assert(p.nonzero == p.samples && p.alpha == p.samples);
	assert(!sweet_surface_probe_bgra(one, 3, 0, 1, 1, 4, &p));
	assert(!sweet_surface_probe_bgra(one, 4, 1, 1, 1, 4, &p));
	assert(!sweet_surface_probe_bgra(nullptr, 4, 0, 1, 1, 4, &p));
	assert(!sweet_surface_probe_bgra(one, 4, 0, 0, 1, 4, &p));
	assert(!sweet_surface_probe_bgra(one, 4, 0, 1, 0, 4, &p));
	assert(!sweet_surface_probe_bgra(one, 4, 0, ~0u, ~0u, ~0u, &p));
	assert(!sweet_surface_probe_bgra(one, 4, 0, 1, 1, 3, &p));
	assert(p.samples == 0);
	puts("surface probe checks passed");
}
