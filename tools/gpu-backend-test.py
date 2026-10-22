#!/usr/bin/env python3
"""Sanitized checks of exported Mali VM/job code with mocked MMIO and memory."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
patch = (root/'kernel/0001-bluejay-port.patch').read_text()
def added(path):
    sec=patch.split(f'diff --git a/{path} b/{path}\n',1)[1].split('\ndiff --git ',1)[0]
    return '\n'.join(x[1:] for x in sec.splitlines() if x.startswith('+') and not x.startswith('+++'))
src=added('bsd/dev/bluejay_gpu.c')
const='\n'.join(x for x in src.splitlines() if x.startswith('#define '))
backend=added('bsd/dev/bluejay_gpu_backend.inc').replace('#include "../../iokit/Kernel/arm/BluejayGPU.h"','')
backend=backend.replace('__asm__ volatile ("mrs %0, cntvct_el0" : "=r" (*value));','*value = 123;').replace('__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (*value));','*value = 24576000;')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t addr64_t;
#define PAGE_SIZE 16384u
#define PAGE_MASK (PAGE_SIZE - 1)
#define KMA_DATA 1
#define KMA_ZERO 2
#define KMA_PERMANENT 4
#define VM_KERN_MEMORY_IOKIT 0
#define KERN_SUCCESS 0
#define TRUE 1
#define FALSE 0
#define kernel_map 0
#define kernel_pmap 0
#define ptoa_64(x) ((uint64_t)(x) << 14)
static void *allocations[140];
static unsigned nalloc, delays, commands;
static bool allocation_fail, command_fail, reset_ok = true;
static unsigned job_mode, last_slot;
static uint32_t regs[0x3000/4];
static int kmem_alloc_contig(int map, vm_offset_t *out, size_t size, unsigned mask, int a, int b, int flags, int tag) {
    (void)map;(void)mask;(void)a;(void)b;(void)flags;(void)tag;
    if(allocation_fail) return 1;
    assert(nalloc < 140);void *p=aligned_alloc(PAGE_SIZE,size); assert(p);memset(p,0,size);
    allocations[nalloc++]=p;*out=(uintptr_t)p;return 0;
}
static uint64_t pmap_find_phys(int map, uintptr_t va) { (void)map;return va >> 14; }
static void flush_dcache64(uint64_t addr, unsigned len, int phys) { assert(addr && len);(void)phys; }
static uint32_t gpu_rd(uint32_t off) { assert(off < sizeof(regs));return regs[off/4]; }
static void gpu_wr(uint32_t off, uint32_t val) {
    assert(off < sizeof(regs));regs[off/4]=val;
    for(unsigned slot=0;slot<2;slot++) if(off==JS_REG(slot,JS_COMMAND_NEXT)) {
        last_slot=slot;
        regs[JOB_INT_RAWSTAT/4]= job_mode==0 ? 1u<<slot : job_mode==1 ? 1u<<(16+slot) : 0;
    }
    if(off==JOB_INT_CLEAR) regs[JOB_INT_RAWSTAT/4]=0;
}
static bool gpu_poll(uint32_t off, uint32_t mask, uint32_t want, unsigned ms) {
    (void)off;(void)mask;(void)want;(void)ms;return reset_ok;
}
static bool bg_as_cmd(uint32_t cmd) { (void)cmd;commands++;return !command_fail; }
static void delay(unsigned us) { assert(us==1000);delays++; }
'''
main=r'''
int main(void) {
    uint64_t pages[]={0x900000000,0x900004000};
    assert(!bluejay_gpu_ready());assert(!bluejay_gpu_map(pages,2,false,false));
    regs[GPU_MMU_FEATURES/4]=0x2830;regs[GPU_ID/4]=0x92020010;
    assert(bv_init());assert(bluejay_gpu_ready());assert(bluejay_gpu_id()==0x92020010);
    uint64_t v;assert(bluejay_gpu_param(0,&v)&&v==0x9202);assert(bluejay_gpu_param(1,&v)&&v==0x10);
    regs[0x4c/4]=7;assert(bluejay_gpu_param(40,&v)&&v==7);assert(!bluejay_gpu_param(999,&v));
    assert(!bluejay_gpu_map(NULL,1,false,false));assert(!bluejay_gpu_map(pages,0,false,false));
    uint64_t bad=0x900000001;assert(!bluejay_gpu_map(&bad,1,false,false));
    bad=1ULL<<40;assert(!bluejay_gpu_map(&bad,1,false,false));
    uint64_t a=bluejay_gpu_map(pages,2,true,true);assert(a==BV_BASE);
    assert(bv_used[0]&&bv_used[1]&&bv_used[2]&&!bv_used[3]);
    for(unsigned i=0;i<8;i++) assert(bv_l3[0][i]==(pages[i/4]+(i%4)*4096)|0); /* check below masks */
    return 0;
}
'''
# PTE address and permissions must preserve every 4KB subpage of a 16KB host page.
main=main.replace('assert(bv_l3[0][i]==(pages[i/4]+(i%4)*4096)|0); /* check below masks */', 'assert((bv_l3[0][i] & 0x000000fffffff000ULL)==pages[i/4]+(i%4)*4096);\n    assert(bv_l3[0][0] & (1ULL<<7));assert(bv_l3[0][0] & (3ULL<<53));')
main=main.replace('    return 0;\n}',r'''
    uint64_t b=bluejay_gpu_map(pages,1,false,false);assert(b==BV_BASE+3*PAGE_SIZE);
    assert(!bluejay_gpu_unmap(0,1));assert(!bluejay_gpu_unmap(BV_BASE+1,1));
    assert(bluejay_gpu_unmap(a,2));assert(!bv_l3[0][0]&&!bv_l3[0][7]);
    assert(bluejay_gpu_map(pages,2,false,false)==a);
    assert(bluejay_gpu_submit(a,false));assert(last_slot==1&&bv_dma_stopped);
    assert(bluejay_gpu_submit(a,true));assert(last_slot==0);
    assert(!bluejay_gpu_submit(a+1,false));
    job_mode=1;assert(!bluejay_gpu_submit(a,false));assert(!bv_online&&bv_dma_stopped);
    assert(bluejay_gpu_unmap(a,2));assert(bluejay_gpu_unmap(b,1));
    bv_online=true;job_mode=2;reset_ok=false;delays=0;
    assert(!bluejay_gpu_submit(a,false));assert(delays==2000&&!bv_dma_stopped);
    assert(!bluejay_gpu_unmap(a,2)); /* retain backing when reset was not confirmed */
    bv_dma_stopped=true;assert(bluejay_gpu_unmap(a,2));
    bv_online=true;command_fail=true;
    assert(!bluejay_gpu_map(pages,1,false,false));assert(!bv_online);
    for(unsigned i=0;i<nalloc;i++) free(allocations[i]);
    puts("PASS GPU VM permissions, guard/reuse, parameters, job slots and fault quarantine (mock hardware)");
    return 0;
}''')
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(const+'\n'+pre+'\n'+backend+'\n'+main)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,stdout=subprocess.PIPE)
print('PASS sanitized Mali backend checks (mock hardware only)')
