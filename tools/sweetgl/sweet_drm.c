/*
 * Panfrost DRM translation over the Pixel SweetGPU IOKit user client.
 * One device per process; jobs complete synchronously in the kernel.
 * Prime descriptors are same-process proxies, valid while their BO lives.
 * Based on joranv1's ios-sailfish libdrm bridge, with permission.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <IOKit/IOKitLib.h>
#include <mach/vm_page_size.h>

#include "xf86drm.h"
#include "drm-uapi/panfrost_drm.h"
#include "SweetGPUABI.h"
#include "sweet_drm.h"

struct sweet_bo {
	uint64_t va, map, size;
	int prime_fd;
	bool live;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static io_connect_t connection;
static int device_fd = -1;
static struct sweet_bo bos[SWEET_GPU_MAX_HANDLES];
static uint32_t next_sync = 1;

static int
error(int value)
{
	errno = value;
	return -value;
}

static struct sweet_bo *
find_bo(uint32_t handle)
{
	return handle > 0 && handle < SWEET_GPU_MAX_HANDLES && bos[handle].live ? &bos[handle] : NULL;
}

static int
scalar(uint32_t selector, const uint64_t *in, uint32_t nin, uint64_t *out, uint32_t nout)
{
	if (!connection) {
		return error(ENODEV);
	}
	uint32_t count = nout;
	kern_return_t kr = IOConnectCallScalarMethod(connection, selector, in, nin, out, &count);
	if (kr != KERN_SUCCESS || count != nout) {
		return error(kr == kIOReturnNoMemory ? ENOMEM : EIO);
	}
	return 0;
}

int
sweet_drm_open(void)
{
	pthread_mutex_lock(&lock);
	if (!connection) {
		io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("SweetGPU"));
		if (service) {
			kern_return_t kr = IOServiceOpen(service, mach_task_self(), 0, &connection);
			IOObjectRelease(service);
			if (kr != KERN_SUCCESS) connection = 0;
		}
		if (connection) {
			uint64_t info[3];
			if (scalar(SWEET_GPU_INFO, NULL, 0, info, 3) ||
			    info[0] != SWEET_GPU_ABI_MAGIC || info[1] != SWEET_GPU_ABI_VERSION) {
				IOServiceClose(connection);
				connection = 0;
			} else {
				device_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
				if (device_fd < 0) {
					IOServiceClose(connection);
					connection = 0;
				}
			}
		}
	}
	int fd = connection && device_fd >= 0 ? dup(device_fd) : -1;
	pthread_mutex_unlock(&lock);
	if (fd < 0) errno = ENODEV;
	return fd;
}

int
drmOpenWithType(const char *name, const char *busid, int type)
{
	(void)busid;
	return name && strcmp(name, "panfrost") == 0 && type == DRM_NODE_RENDER ? sweet_drm_open() : -1;
}

static int
add_bo(uint64_t handle, uint64_t va, uint64_t map, uint64_t size)
{
	if (handle == 0 || handle >= SWEET_GPU_MAX_HANDLES || bos[handle].live ||
	    va == 0 || map == 0 || size == 0 || size > SWEET_GPU_MAX_BO_BYTES) {
		return error(EINVAL);
	}
	bos[handle] = (struct sweet_bo){ .va = va, .map = map, .size = size, .prime_fd = -1, .live = true };
	return 0;
}

static int
close_bo(uint32_t handle)
{
	struct sweet_bo *bo = find_bo(handle);
	if (!bo) return error(ENOENT);
	uint64_t in = handle;
	int ret = scalar(SWEET_GPU_FREE_BO, &in, 1, NULL, 0);
	if (ret) return ret;
	if (bo->prime_fd >= 0) close(bo->prime_fd);
	memset(bo, 0, sizeof(*bo));
	return 0;
}

int
drmCloseBufferHandle(int fd, uint32_t handle)
{
	(void)fd;
	pthread_mutex_lock(&lock);
	int ret = close_bo(handle);
	pthread_mutex_unlock(&lock);
	return ret;
}

static int
create_bo(struct drm_panfrost_create_bo *r)
{
	if (!r->size || r->size > SWEET_GPU_MAX_BO_BYTES || r->pad ||
	    (r->flags & ~(PANFROST_BO_NOEXEC | PANFROST_BO_HEAP))) return error(EINVAL);
	uint64_t in[2] = { r->size, r->flags }, out[4];
	int ret = scalar(SWEET_GPU_CREATE_BO, in, 2, out, 4);
	if (ret) return ret;
	ret = add_bo(out[0], out[1], out[2], out[3]);
	if (ret) {
		scalar(SWEET_GPU_FREE_BO, out, 1, NULL, 0);
		return ret;
	}
	r->handle = (uint32_t)out[0];
	r->offset = out[1];
	r->size = (uint32_t)out[3];
	return 0;
}

static int
submit(struct drm_panfrost_submit *r)
{
	if (!r->jc || (r->jc & 63) || !r->bo_handle_count ||
	    r->bo_handle_count > SWEET_GPU_MAX_SUBMIT_BOS || !r->bo_handles ||
	    r->in_sync_count > SWEET_GPU_MAX_SUBMIT_BOS ||
	    (r->in_sync_count && !r->in_syncs) || r->pad || r->jm_ctx_handle ||
	    (r->requirements & ~PANFROST_JD_REQ_FS)) return error(EINVAL);
	struct {
		struct sweet_gpu_submit header;
		uint32_t handles[SWEET_GPU_MAX_SUBMIT_BOS];
	} input = { .header = { r->jc, r->requirements, r->bo_handle_count } };
	const uint32_t *handles = (const void *)(uintptr_t)r->bo_handles;
	for (uint32_t i = 0; i < r->bo_handle_count; i++) {
		if (!find_bo(handles[i])) return error(ENOENT);
		input.handles[i] = handles[i];
	}
	/* The synchronous kernel call finishes before output syncs become visible. */
	kern_return_t kr = IOConnectCallStructMethod(connection, SWEET_GPU_SUBMIT, &input,
	    sizeof(input.header) + r->bo_handle_count * sizeof(uint32_t), NULL, NULL);
	return kr == KERN_SUCCESS ? 0 : error(EIO);
}

static int
pan_ioctl(unsigned long index, void *data)
{
	if (!data) return error(EINVAL);
	switch (index) {
	case DRM_PANFROST_GET_PARAM: {
		struct drm_panfrost_get_param *r = data;
		if (r->pad) return error(EINVAL);
		uint64_t in = r->param, out;
		int ret = scalar(SWEET_GPU_GET_PARAM, &in, 1, &out, 1);
		if (!ret) r->value = out;
		return ret;
	}
	case DRM_PANFROST_CREATE_BO:
		return create_bo(data);
	case DRM_PANFROST_MMAP_BO: {
		struct drm_panfrost_mmap_bo *r = data;
		if (r->flags || !find_bo(r->handle)) return error(EINVAL);
		r->offset = (uint64_t)r->handle << 32;
		return 0;
	}
	case DRM_PANFROST_GET_BO_OFFSET: {
		struct drm_panfrost_get_bo_offset *r = data;
		struct sweet_bo *bo = find_bo(r->handle);
		if (r->pad || !bo) return error(EINVAL);
		r->offset = bo->va;
		return 0;
	}
	case DRM_PANFROST_WAIT_BO: {
		struct drm_panfrost_wait_bo *r = data;
		if (r->pad || !find_bo(r->handle)) return error(EINVAL);
		uint64_t in = r->handle;
		return scalar(SWEET_GPU_SYNC_BO, &in, 1, NULL, 0);
	}
	case DRM_PANFROST_SUBMIT:
		return submit(data);
	case DRM_PANFROST_MADVISE: {
		struct drm_panfrost_madvise *r = data;
		if (!find_bo(r->handle) || r->madv > PANFROST_MADV_DONTNEED) return error(EINVAL);
		r->retained = 1; /* wired eager backing; no discard */
		return 0;
	}
	case DRM_PANFROST_SET_LABEL_BO: {
		struct drm_panfrost_set_label_bo *r = data;
		return find_bo(r->handle) ? 0 : error(ENOENT);
	}
	default:
		return error(ENOTSUP);
	}
}

int
drmCommandWriteRead(int fd, unsigned long index, void *data, unsigned long size)
{
	(void)fd;
	(void)size;
	pthread_mutex_lock(&lock);
	int ret = connection ? pan_ioctl(index, data) : error(ENODEV);
	pthread_mutex_unlock(&lock);
	return ret;
}

int
drmCommandWrite(int fd, unsigned long index, void *data, unsigned long size)
{
	return drmCommandWriteRead(fd, index, data, size);
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
	if (!arg) return error(EINVAL);
	if (request == DRM_IOCTL_GEM_CLOSE) return drmCloseBufferHandle(fd, ((struct drm_gem_close *)arg)->handle);
	unsigned nr = request & 0xff;
	if (((request >> 8) & 0xff) == DRM_IOCTL_BASE && nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END)
		return drmCommandWriteRead(fd, nr - DRM_COMMAND_BASE, arg, 0);
	return error(ENOTSUP);
}

void *
sweet_drm_bo_map(int fd, uint64_t offset)
{
	(void)fd;
	pthread_mutex_lock(&lock);
	struct sweet_bo *bo = (offset & UINT32_MAX) == 0 ? find_bo((uint32_t)(offset >> 32)) : NULL;
	void *p = bo ? (void *)(uintptr_t)bo->map : MAP_FAILED;
	pthread_mutex_unlock(&lock);
	return p;
}

int
sweet_drm_import_iosurface(uint32_t surface_id, uint64_t map, uint32_t *handle, uint64_t *size)
{
	if (!surface_id || !map || !handle || !size) return error(EINVAL);
	pthread_mutex_lock(&lock);
	uint64_t in = surface_id, out[3];
	int ret = scalar(SWEET_GPU_IMPORT_SURFACE, &in, 1, out, 3);
	if (!ret) {
		/* The resource offset carries the pixels' displacement within this
		 * page. Mesa maps the complete BO, so expose its page base here. */
		ret = add_bo(out[0], out[1], map & ~(uint64_t)vm_page_mask, out[2]);
		if (ret) scalar(SWEET_GPU_FREE_BO, out, 1, NULL, 0);
		else { *handle = (uint32_t)out[0]; *size = out[2]; }
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

static drmVersion version = {
	.version_major = 1, .version_minor = 1, .version_patchlevel = 0,
	.name_len = 8, .name = "panfrost", .date_len = 0, .date = "", .desc_len = 0, .desc = "",
};

drmVersionPtr drmGetVersion(int fd) { (void)fd; return connection ? &version : NULL; }
void drmFreeVersion(drmVersionPtr v) { (void)v; }

int
drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	(void)fd;
	if (!value) return error(EINVAL);
	*value = capability == DRM_CAP_SYNCOBJ || capability == DRM_CAP_SYNCOBJ_TIMELINE;
	return 0;
}

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)fd; (void)flags;
	if (!prime_fd) return error(EINVAL);
	pthread_mutex_lock(&lock);
	struct sweet_bo *bo = find_bo(handle);
	int ret = error(ENOENT);
	if (bo) {
		if (bo->prime_fd < 0) {
			char path[] = "/tmp/sweetgl-prime-XXXXXX";
			bo->prime_fd = mkstemp(path);
			if (bo->prime_fd >= 0) {
				unlink(path);
				fcntl(bo->prime_fd, F_SETFD, FD_CLOEXEC);
				if (ftruncate(bo->prime_fd, (off_t)bo->size)) {
					close(bo->prime_fd); bo->prime_fd = -1;
				}
			}
		}
		*prime_fd = bo->prime_fd >= 0 ? dup(bo->prime_fd) : -1;
		ret = *prime_fd >= 0 ? 0 : error(EMFILE);
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
	(void)fd;
	struct stat incoming;
	if (!handle || fstat(prime_fd, &incoming)) return error(EINVAL);
	pthread_mutex_lock(&lock);
	int ret = error(ENOENT);
	for (uint32_t h = 1; h < SWEET_GPU_MAX_HANDLES; h++) {
		struct stat candidate;
		if (bos[h].live && bos[h].prime_fd >= 0 && !fstat(bos[h].prime_fd, &candidate) &&
		    incoming.st_dev == candidate.st_dev && incoming.st_ino == candidate.st_ino) {
			*handle = h; ret = 0; break;
		}
	}
	pthread_mutex_unlock(&lock);
	return ret;
}

/* No asynchronous jobs: every published synchronization point is complete. */
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
	(void)fd; (void)flags;
	if (!handle) return error(EINVAL);
	pthread_mutex_lock(&lock); *handle = next_sync++; pthread_mutex_unlock(&lock);
	return *handle ? 0 : error(ENOSPC);
}
int drmSyncobjDestroy(int fd, uint32_t handle) { (void)fd; (void)handle; return 0; }
int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count)
{ (void)fd; (void)handles; (void)count; return 0; }
int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count)
{ (void)fd; (void)handles; (void)count; return 0; }
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points, uint32_t count)
{ (void)fd; (void)handles; (void)points; (void)count; return 0; }
int drmSyncobjWait(int fd, uint32_t *handles, unsigned count, int64_t timeout, unsigned flags, uint32_t *first)
{ (void)fd; (void)handles; (void)timeout; (void)flags; if (!count) return error(EINVAL); if (first) *first = 0; return 0; }
int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned count, int64_t timeout, unsigned flags, uint32_t *first)
{ (void)points; return drmSyncobjWait(fd, handles, count, timeout, flags, first); }
int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t count, uint32_t flags)
{ (void)fd; (void)handles; (void)flags; for (uint32_t i = 0; i < count; i++) points[i] = UINT64_MAX; return 0; }
int drmSyncobjTransfer(int fd, uint32_t dst, uint64_t dp, uint32_t src, uint64_t sp, uint32_t flags)
{ (void)fd; (void)dst; (void)dp; (void)src; (void)sp; (void)flags; return 0; }
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_fd)
{ (void)fd; (void)handle; if (!sync_fd) return error(EINVAL); *sync_fd = open("/dev/null", O_RDONLY | O_CLOEXEC); return *sync_fd < 0 ? error(EMFILE) : 0; }
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_fd)
{ (void)fd; (void)handle; return fcntl(sync_fd, F_GETFD) < 0 ? error(EINVAL) : 0; }
int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{ return drmSyncobjExportSyncFile(fd, handle, obj_fd); }
int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{ if (fcntl(obj_fd, F_GETFD) < 0) return error(EINVAL); return drmSyncobjCreate(fd, 0, handle); }
