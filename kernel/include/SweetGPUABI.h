/* Pixel Mali/Panfrost user-client contract. Shared by XNU and sweetgl. */
#ifndef SWEET_GPU_ABI_H
#define SWEET_GPU_ABI_H

#include <stdint.h>

#define SWEET_GPU_ABI_MAGIC 0x4d414c49u /* MALI; reject the unrelated MSM ABI */
#define SWEET_GPU_ABI_VERSION 1u
#define SWEET_GPU_MAX_HANDLES 4096u
#define SWEET_GPU_MAX_SUBMIT_BOS 512u
#define SWEET_GPU_MAX_BO_BYTES (128u << 20)
#define SWEET_GPU_MAX_WIRED_BYTES (512u << 20)

enum sweet_gpu_selector {
	SWEET_GPU_CREATE_BO = 10, /* in: size, flags; out: handle, GPU VA, CPU VA, size */
	SWEET_GPU_FREE_BO = 11,   /* in: handle */
	SWEET_GPU_IMPORT_SURFACE = 12, /* in: IOSurface ID; out: handle, GPU VA, size */
	SWEET_GPU_SUBMIT = 13,    /* struct sweet_gpu_submit + handles; synchronous */
	SWEET_GPU_WAIT_BO = 14,   /* in: handle; completed synchronous work */
	SWEET_GPU_INFO = 15,      /* out: magic, ABI version, full GPU_ID */
	SWEET_GPU_SYNC_BO = 16,   /* in: handle; invalidate CPU caches after GPU work */
	SWEET_GPU_GET_PARAM = 17, /* in: drm_panfrost_param; out: value */
};

/* CREATE_BO accepts Panfrost NOEXEC and HEAP. HEAP is eagerly backed. */
#define SWEET_GPU_BO_NOEXEC 1u
#define SWEET_GPU_BO_HEAP 2u

struct sweet_gpu_submit {
	uint64_t job;
	uint32_t requirements;
	uint32_t handle_count;
	/* uint32_t handles[handle_count] immediately follow this header. */
};

#endif
