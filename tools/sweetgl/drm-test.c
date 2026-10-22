/* Host-only IOKit mocks: exercises the actual DRM translator, not the GPU. */
#define IOConnectCallScalarMethod test_scalar
#define IOConnectCallStructMethod test_struct
#include "sweet_drm.c"
#include <assert.h>

static uint32_t mock_handle = 1;
static uint32_t scalar_calls, submit_calls;
static bool fail_submit, fail_free;

kern_return_t
test_scalar(mach_port_t c, uint32_t selector, const uint64_t *in,
    uint32_t nin, uint64_t *out, uint32_t *nout)
{
	assert(c == 1);
	scalar_calls++;
	switch (selector) {
	case SWEET_GPU_CREATE_BO:
		assert(nin == 2 && *nout == 4);
		out[0] = mock_handle; out[1] = 0x1000000;
		out[2] = 0x2000000; out[3] = (in[0] + 16383) & ~16383ull;
		return KERN_SUCCESS;
	case SWEET_GPU_FREE_BO:
		assert(nin == 1 && *nout == 0);
		return fail_free ? kIOReturnError : KERN_SUCCESS;
	case SWEET_GPU_GET_PARAM:
		assert(nin == 1 && *nout == 1);
		out[0] = in[0] == DRM_PANFROST_PARAM_GPU_PROD_ID ? 0x9202 : 0;
		return KERN_SUCCESS;
	case SWEET_GPU_SYNC_BO:
		assert(nin == 1 && *nout == 0);
		return KERN_SUCCESS;
	default:
		assert(!"unexpected selector");
		return kIOReturnUnsupported;
	}
}

kern_return_t
test_struct(mach_port_t c, uint32_t selector, const void *in, size_t size,
    void *out, size_t *nout)
{
	assert(c == 1 && selector == SWEET_GPU_SUBMIT && !out && !nout);
	const struct sweet_gpu_submit *r = in;
	assert(size == sizeof(*r) + r->handle_count * sizeof(uint32_t));
	assert(r->job == 0x1000000 && r->handle_count == 1);
	assert(*(const uint32_t *)(r + 1) == 1);
	submit_calls++;
	return fail_submit ? kIOReturnError : KERN_SUCCESS;
}

int
main(void)
{
	connection = 1;
	struct drm_panfrost_create_bo create = { .size = 1 };
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_CREATE_BO, &create) == 0);
	assert(create.handle == 1 && create.size == 16384 && create.offset == 0x1000000);
	assert(sweet_drm_bo_map(-1, 1ull << 32) == (void *)0x2000000);
	assert(sweet_drm_bo_map(-1, (1ull << 32) | 1) == MAP_FAILED);
	assert(sweet_drm_bo_map(-1, (uint64_t)SWEET_GPU_MAX_HANDLES << 32) == MAP_FAILED);

	struct drm_panfrost_get_param param = { .param = DRM_PANFROST_PARAM_GPU_PROD_ID };
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_GET_PARAM, &param) == 0 && param.value == 0x9202);
	param.pad = 1;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_GET_PARAM, &param) == -EINVAL);

	uint32_t calls = scalar_calls;
	for (uint32_t i = 0; i < 4; i++) {
		struct drm_panfrost_create_bo bad = { .size = 16384 };
		if (i == 0) bad.size = 0;
		if (i == 1) bad.size = SWEET_GPU_MAX_BO_BYTES + 1;
		if (i == 2) bad.flags = PANFROST_BO_WB_MMAP;
		if (i == 3) bad.pad = 1;
		assert(drmIoctl(-1, DRM_IOCTL_PANFROST_CREATE_BO, &bad) == -EINVAL);
	}
	assert(calls == scalar_calls);

	int proxy = -1, duplicate;
	uint32_t imported = 0;
	assert(drmPrimeHandleToFD(-1, 1, 0, &proxy) == 0 && proxy >= 0);
	assert(lseek(proxy, 0, SEEK_END) == 16384);
	duplicate = dup(proxy);
	assert(duplicate >= 0);
	assert(drmPrimeFDToHandle(-1, duplicate, &imported) == 0 && imported == 1);
	int unrelated = open("/dev/null", O_RDONLY);
	assert(unrelated >= 0);
	assert(drmPrimeFDToHandle(-1, unrelated, &imported) == -ENOENT);
	close(unrelated);

	uint32_t handles[1] = {1};
	struct drm_panfrost_submit job = {
		.jc = 0x1000000, .bo_handles = (uintptr_t)handles, .bo_handle_count = 1,
	};
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_SUBMIT, &job) == 0 && submit_calls == 1);
	fail_submit = true;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_SUBMIT, &job) == -EIO);
	fail_submit = false;
	job.bo_handle_count = SWEET_GPU_MAX_SUBMIT_BOS + 1;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_SUBMIT, &job) == -EINVAL);
	job.bo_handle_count = 1; handles[0] = 0;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_SUBMIT, &job) == -ENOENT);
	handles[0] = 1; job.jc++;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_SUBMIT, &job) == -EINVAL);
	assert(submit_calls == 2);

	fail_free = true;
	assert(drmCloseBufferHandle(-1, 1) == -EIO && find_bo(1));
	fail_free = false;
	assert(drmCloseBufferHandle(-1, 1) == 0 && !find_bo(1));
	assert(drmCloseBufferHandle(-1, 1) == -ENOENT);
	assert(drmPrimeFDToHandle(-1, duplicate, &imported) == -ENOENT);
	assert(sweet_drm_bo_map(-1, 1ull << 32) == MAP_FAILED);
	close(proxy); close(duplicate);
	connection = 0;
	assert(drmIoctl(-1, DRM_IOCTL_PANFROST_GET_PARAM, &param) == -ENODEV);
	return 0;
}
