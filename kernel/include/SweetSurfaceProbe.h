/* Bounded, read-only BGRA sampling for the temporary graphics diagnostic.
 * Counts describe sampled pixels, not a proof that the whole image is blank.
 * No pixel values or page text are emitted. Integer types come from caller. */
#ifndef SWEET_SURFACE_PROBE_H
#define SWEET_SURFACE_PROBE_H
struct SweetSurfaceProbe {
	unsigned samples, nonzero, rgb, alpha;
};

static inline void
sweet_surface_probe_pixel(const volatile unsigned char *p, SweetSurfaceProbe *r)
{
	unsigned rgb = p[0] | p[1] | p[2], alpha = p[3];
	r->samples++;
	r->nonzero += (rgb | alpha) != 0;
	r->rgb += rgb != 0;
	r->alpha += alpha != 0;
}

static inline bool
sweet_surface_probe_bgra(const volatile unsigned char *base, unsigned long long length,
    unsigned offset, unsigned width, unsigned height, unsigned stride, SweetSurfaceProbe *r)
{
	*r = {};
	if (!base || !width || !height || width > 16384 || height > 16384 ||
	    stride < width * 4u ||
	    (unsigned long long)offset + (unsigned long long)(height - 1) * stride +
	    (unsigned long long)width * 4 > length) {
		return false;
	}
	/* A whole-image grid, plus a denser top strip for plain-text test pages. */
	for (unsigned gy = 0; gy < 64; gy++) {
		unsigned y = (height - 1) * gy / 63;
		for (unsigned gx = 0; gx < 64; gx++) {
			unsigned x = (width - 1) * gx / 63;
			sweet_surface_probe_pixel(base + offset + (unsigned long long)y * stride + x * 4, r);
		}
	}
	for (unsigned y = 0; y < height && y < 192; y += 4) {
		for (unsigned x = 0; x < width; x += 8) {
			sweet_surface_probe_pixel(base + offset + (unsigned long long)y * stride + x * 4, r);
		}
	}
	return true;
}
#endif
