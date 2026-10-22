#ifndef BLUEJAY_GPU_H
#define BLUEJAY_GPU_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Caller serializes all calls with the SweetGPU service lock. */
bool bluejay_gpu_ready(void);
uint32_t bluejay_gpu_id(void);
bool bluejay_gpu_param(uint32_t param, uint64_t *value);
uint64_t bluejay_gpu_map(const uint64_t *pages, uint32_t count, bool readonly, bool noexec);
bool bluejay_gpu_unmap(uint64_t va, uint32_t count);
bool bluejay_gpu_submit(uint64_t job, bool fragment);
#ifdef __cplusplus
}
#endif
#endif
