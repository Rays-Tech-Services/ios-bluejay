/* Geometry policy shared by the kernel and the host-side checks. */
#ifndef SWEET_SURFACE_FORMAT_H
#define SWEET_SURFACE_FORMAT_H

#define SWEET_FORMAT_BGRA 0x42475241u
#define SWEET_FORMAT_RGB10 0x77333072u
#define SWEET_FORMAT_RGB10A8 0x62336138u

static inline unsigned
sweet_surface_plane_default_bpe(unsigned format, unsigned plane)
{
	/* WebKit supplies stride/offset/size, but omits PlaneBytesPerElement. */
	return format == SWEET_FORMAT_RGB10A8 && plane == 0 ? 4 : 1;
}

static inline bool
sweet_surface_web_bgra(bool enabled, const char *process, unsigned format,
    unsigned parent)
{
	return enabled && parent == 0 &&
	       (format == SWEET_FORMAT_RGB10 || format == SWEET_FORMAT_RGB10A8) &&
	       (!__builtin_strcmp(process, "com.apple.WebKit.WebContent") ||
	        !__builtin_strcmp(process, "com.apple.WebKit.GPU"));
}

struct SweetSurfaceBGRALayout {
	unsigned rowBytes, allocSize;
};

static inline bool
sweet_surface_bgra_layout(unsigned width, unsigned height, unsigned pageSize,
    SweetSurfaceBGRALayout *result)
{
	if (!width || !height || width > 16384 || height > 16384 ||
	    pageSize < 64 || pageSize > 65536 || (pageSize & (pageSize - 1))) {
		return false;
	}
	unsigned row = (width * 4u + 63u) & ~63u;
	unsigned long long size = (unsigned long long)row * height;
	size = (size + pageSize - 1) & ~(unsigned long long)(pageSize - 1);
	if (size > (256u << 20)) {
		return false;
	}
	result->rowBytes = row;
	result->allocSize = (unsigned)size;
	return true;
}
#endif
