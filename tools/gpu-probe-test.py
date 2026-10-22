#!/usr/bin/env python3
"""Host tests of real GPU probe gates; MMIO and scheduler calls are mocked."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
patch = (ROOT / 'kernel/0001-bluejay-port.patch').read_text()
section = patch.split('diff --git a/bsd/dev/bluejay_gpu.c b/bsd/dev/bluejay_gpu.c\n', 1)[1]
section = section.split('\ndiff --git ', 1)[0]
source = '\n'.join(line[1:] for line in section.splitlines()
                   if line.startswith('+') and not line.startswith('+++'))


def function(name):
    start = source.index(name + '(')
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


constants = '\n'.join(line for line in source.splitlines()
                      if any(line.startswith('#define ' + name + ' ')
                             for name in ['PD_G3D_PA', 'G3D_PMU_STATUS_PA', 'GPU_PA', 'GPU_SIZE', 'GPU_ID']))
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
typedef int wait_result_t;
typedef int boolean_t;
typedef int kern_return_t;
typedef int thread_t;
typedef void *DTEntry;
typedef void *event_t;
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
#define NSEC_PER_MSEC 1000000
#define KERN_SUCCESS 0
#define kSuccess 1
static unsigned char pmu[16384] __attribute__((aligned(16384)));
static unsigned char gpu[32768] __attribute__((aligned(16384)));
static vm_offset_t bg_regs;
static int level, seconds, board, property, qsize, power_ok;
static uint32_t qvalue;
static unsigned maps, features, powers, jobs, starts, sleeps, interval, initialized;
static bool job_ok;
static bool fail_pmu_map, fail_gpu_map, fail_start;
static int PE_parse_boot_argn(const char *name, void *value, size_t size) {
    assert(size == sizeof(int));
    *(int *)value = !strcmp(name, "sweetgpu") ? level : seconds;
    return 1;
}
static int SecureDTLookupEntry(void *root, const char *path, DTEntry *entry) {
    (void)root; assert(!strcmp(path, "/bluejay")); *entry = pmu;
    return board ? kSuccess : 0;
}
static int SecureDTGetProperty(DTEntry entry, const char *name, const void **value, unsigned *size) {
    assert(entry == pmu && !strcmp(name, "qemu"));
    *value = &qvalue; *size = (unsigned)qsize;
    return property ? kSuccess : 0;
}
static vm_offset_t ml_io_map(vm_offset_t address, vm_size_t size) {
    maps++;
    if (address == (PD_G3D_PA & ~0x3fffULL)) {
        assert(size == sizeof(pmu)); return fail_pmu_map ? 0 : (uintptr_t)pmu;
    }
    assert(address == GPU_PA && size == sizeof(gpu));
    return fail_gpu_map ? 0 : (uintptr_t)gpu;
}
static void assert_wait_timeout(void *event, int kind, uint32_t value, uint32_t scale) {
    (void)event; assert(kind == THREAD_UNINT && scale == NSEC_PER_MSEC);
    sleeps++; interval = value;
}
static void thread_block(void *continuation) { assert(!continuation); }
static uint32_t gpu_rd(uint32_t offset) { assert(offset == GPU_ID); return *(uint32_t *)gpu; }
static void bg_features(void) { features++; }
static bool bg_power(void) { powers++; return power_ok; }
static bool bg_job_success;
static bool bv_init(void) { initialized++; return true; }
static void bg_job_test(void) { jobs++; bg_job_success = job_ok; }
static int kernel_thread_start(void (*fn)(void *, int), void *arg, thread_t *thread) {
    starts++; if (fail_start) return 1; *thread = 1; fn(arg, 0); return 0;
}
static void thread_deallocate(thread_t thread) { assert(thread == 1); }
'''
main = r'''
static void reset(void) {
    memset(pmu, 0, sizeof(pmu)); memset(gpu, 0, sizeof(gpu));
    level = 1; seconds = 0; board = property = 1; qsize = 4; qvalue = 0;
    power_ok = 1; maps = features = powers = jobs = starts = sleeps = interval = initialized = 0;
    job_ok = true; bg_job_success = false;
    fail_pmu_map = fail_gpu_map = fail_start = false;
    *(uint32_t *)(pmu + 0x1e00) = 1; *(uint32_t *)(pmu + 0x1e04) = 1;
    *(uint32_t *)(pmu + 0x2004) = 1; *(uint32_t *)gpu = 0x92020010;
}
int main(void) {
    reset(); level = 0; bluejay_gpu_start(); assert(!starts && !maps);
    reset(); board = 0; bluejay_gpu_start(); assert(!starts && !maps);
    reset(); property = 0; bluejay_gpu_start(); assert(!starts && !maps);
    reset(); qsize = 8; bluejay_gpu_start(); assert(!starts && !maps);
    reset(); qvalue = 1; bluejay_gpu_start(); assert(!starts && !maps);
    reset(); fail_start = true; bluejay_gpu_start(); assert(starts == 1 && !maps);
    reset(); fail_pmu_map = true; bluejay_gpu_start(); assert(maps == 1 && !features);
    reset(); *(uint32_t *)(pmu + 0x1e04) = 0; bluejay_gpu_start(); assert(maps == 1 && !features);
    reset(); *(uint32_t *)(pmu + 0x1e04) = UINT32_MAX; bluejay_gpu_start(); assert(maps == 1 && !features);
    reset(); fail_gpu_map = true; bluejay_gpu_start(); assert(maps == 2 && !features);
    reset(); *(uint32_t *)gpu = 0; bluejay_gpu_start(); assert(maps == 2 && !features);
    reset(); *(uint32_t *)gpu = UINT32_MAX; bluejay_gpu_start(); assert(maps == 2 && !features);
    reset(); bluejay_gpu_start(); assert(features == 1 && !powers && !jobs);
    reset(); level = 2; bluejay_gpu_start(); assert(features == 1 && powers == 1 && !jobs);
    reset(); level = 3; power_ok = 0; bluejay_gpu_start(); assert(powers == 1 && !jobs);
    reset(); level = 3; bluejay_gpu_start(); assert(powers == 1 && jobs == 1);
    reset(); seconds = -1; bluejay_gpu_start(); assert(!sleeps);
    reset(); seconds = 301; bluejay_gpu_start(); assert(!sleeps);
    reset(); seconds = 300; bluejay_gpu_start(); assert(sleeps == 1 && interval == 300000);
    reset(); seconds = 30; bluejay_gpu_start(); assert(sleeps == 1 && interval == 30000);
    reset(); level = 4; bluejay_gpu_start(); assert(jobs == 1 && initialized == 1);
    reset(); level = 4; job_ok = false; bluejay_gpu_start(); assert(jobs == 1 && !initialized);
    reset(); level = 4; power_ok = 0; bluejay_gpu_start(); assert(!jobs && !initialized);
    return 0;
}
'''
(ROOT / 'out').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='gpu-guard-test-', dir=ROOT / 'out') as temp:
    path = Path(temp)
    cfile = path / 'probe.c'
    cfile.write_text(constants + '\n' + prelude + '\nstatic void\n' +
                     function('bluejay_gpu_thread') + '\nvoid\n' +
                     function('bluejay_gpu_start') + main)
    binary = path / 'probe'
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, capture_output=True)
print('PASS 23 sanitized GPU probe gate cases (mock hardware only)')
