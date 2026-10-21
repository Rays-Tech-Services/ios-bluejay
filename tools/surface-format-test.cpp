#include <cassert>
#include <cstdio>
#include "../kernel/include/SweetSurfaceFormat.h"

int main()
{
	assert(sweet_surface_plane_default_bpe(SWEET_FORMAT_RGB10A8, 0) == 4);
	assert(sweet_surface_plane_default_bpe(SWEET_FORMAT_RGB10A8, 1) == 1);
	assert(sweet_surface_plane_default_bpe(0x34323076, 0) == 1);
	assert(sweet_surface_web_bgra(true, "com.apple.WebKit.GPU", SWEET_FORMAT_RGB10A8, 0));
	assert(sweet_surface_web_bgra(true, "com.apple.WebKit.WebContent", SWEET_FORMAT_RGB10, 0));
	assert(!sweet_surface_web_bgra(false, "com.apple.WebKit.GPU", SWEET_FORMAT_RGB10A8, 0));
	assert(!sweet_surface_web_bgra(true, "SpringBoard", SWEET_FORMAT_RGB10, 0));
	assert(!sweet_surface_web_bgra(true, "com.apple.WebKit.GPU.fake", SWEET_FORMAT_RGB10, 0));
	assert(!sweet_surface_web_bgra(true, "com.apple.WebKit.GPU", SWEET_FORMAT_BGRA, 0));
	assert(!sweet_surface_web_bgra(true, "com.apple.WebKit.GPU", SWEET_FORMAT_RGB10A8, 5));
	assert(sweet_surface_web_cpu(true, "com.apple.WebKit.GPU", 0));
	assert(sweet_surface_web_cpu(true, "com.apple.WebKit.WebContent", 0));
	assert(!sweet_surface_web_cpu(false, "com.apple.WebKit.GPU", 0));
	assert(!sweet_surface_web_cpu(true, "MobileSafari", 0));
	assert(!sweet_surface_web_cpu(true, "backboardd", 0));
	assert(!sweet_surface_web_cpu(true, "com.apple.WebKit.GPU.fake", 0));
	assert(!sweet_surface_web_cpu(true, "com.apple.WebKit.GPU", 5));
	SweetSurfaceBGRALayout l;
	assert(sweet_surface_bgra_layout(1125, 1536, 16384, &l));
	assert(l.rowBytes == 4544 && l.allocSize == 6979584);
	assert(sweet_surface_bgra_layout(1, 1, 4096, &l));
	assert(l.rowBytes == 64 && l.allocSize == 4096);
	assert(!sweet_surface_bgra_layout(0, 100, 16384, &l));
	assert(!sweet_surface_bgra_layout(100, 0, 16384, &l));
	assert(!sweet_surface_bgra_layout(~0u, ~0u, 16384, &l));
	assert(!sweet_surface_bgra_layout(16384, 16384, 16384, &l));
	assert(!sweet_surface_bgra_layout(100, 100, 6000, &l));
	assert(!sweet_surface_bgra_layout(100, 100, 0, &l));
	/* Every accepted row and allocation must cover the entire visible image. */
	for (unsigned w = 1; w <= 16384; w += 127) {
		for (unsigned h = 1; h <= 16384; h += 131) {
			if (sweet_surface_bgra_layout(w, h, 16384, &l)) {
				assert(l.rowBytes >= w * 4 && !(l.rowBytes & 63));
				assert(l.allocSize >= (unsigned long long)l.rowBytes * h);
				assert(!(l.allocSize & 16383) && l.allocSize <= (256u << 20));
			}
		}
	}
	puts("surface format checks passed");
}
