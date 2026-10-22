#!/usr/bin/env python3
"""Test exported SweetGPU buffer lifecycle and submit validation with fake IOKit."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
patch=(root/'kernel/0001-bluejay-port.patch').read_text()
sec=patch.split('diff --git a/iokit/Kernel/arm/SweetGPU.cpp b/iokit/Kernel/arm/SweetGPU.cpp\n',1)[1].split('\ndiff --git ',1)[0]
src='\n'.join(x[1:] for x in sec.splitlines() if x.startswith('+') and not x.startswith('+++'))
def function(name):
    start=src.index(name+'(');end=src.index('{',start)+1;depth=1
    while depth:
        depth+=(src[end]=='{')-(src[end]=='}');end+=1
    return src[start:end]
bo=src[src.index('struct SweetBO {'):src.index('static IOLock *gpu_lock;')]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "SweetGPUABI.h"
#define PAGE_SIZE 16384u
#define PAGE_MASK 16383u
#define TRUE 1
#define kernel_task 0
#define kIODirectionInOut 1
#define kIOMemoryKernelUserShared 2
#define kIOMapInhibitCache 4
#define kIOMapAnywhere 8
#define kIOMemoryMapperNone 16
#define round_page_64(x) (((x)+PAGE_MASK)&~(uint64_t)PAGE_MASK)
#define OSSafeReleaseNULL(p) do { if(p) {(p)->release();(p)=nullptr;} } while(0)
using IOByteCount=uint64_t;using addr64_t=uint64_t;using IOReturn=int;
enum { kIOReturnSuccess, kIOReturnBadArgument, kIOReturnNoResources, kIOReturnNoMemory,
       kIOReturnVMError, kIOReturnIOError };
static int liveMem, liveMap, liveSurface, prepared;
static bool failPrepare, failMapping, failSegment, failGPUMap, failUnmap, failJob;
static bool mappedReadonly, mappedNoexec, submittedFragment;
static unsigned submissions;
static uint64_t nextVA=0x40000000;
struct IOMemoryMap {
    IOMemoryMap(){liveMap++;} void release(){liveMap--;delete this;}
    uint64_t getVirtualAddress(){return 0x700000000;}
};
struct IOMemoryDescriptor {
    unsigned refs=1;uint64_t size;void *bytes;
    explicit IOMemoryDescriptor(uint64_t n):size(n),bytes(calloc(1,(size_t)n)){assert(bytes);liveMem++;}
    virtual ~IOMemoryDescriptor(){free(bytes);liveMem--;}
    void retain(){refs++;} void release(){if(!--refs)delete this;}
    uint64_t getLength(){return size;}
    IOReturn prepare(){if(failPrepare)return kIOReturnVMError;prepared++;return kIOReturnSuccess;}
    void complete(){assert(prepared>0);prepared--;}
    uint64_t getPhysicalSegment(uint64_t off, IOByteCount *len, unsigned opts) {
        assert(opts==kIOMemoryMapperNone);*len=failSegment?1:size-off;return 0x900000000+off;
    }
    IOMemoryMap *createMappingInTask(int task,int addr,unsigned opts){
        (void)task;(void)addr;assert(opts==(kIOMapAnywhere|kIOMapInhibitCache));
        return failMapping?nullptr:new IOMemoryMap;
    }
};
struct IOBufferMemoryDescriptor: IOMemoryDescriptor {
    explicit IOBufferMemoryDescriptor(uint64_t n):IOMemoryDescriptor(n){}
    static IOBufferMemoryDescriptor *inTaskWithOptions(int task,unsigned opts,uint64_t n,unsigned align){
        (void)task;(void)opts;assert(align==PAGE_SIZE);return new IOBufferMemoryDescriptor(n);
    }
    void *getBytesNoCopy(){return bytes;}
};
struct SweetSurface {
    unsigned refs=1;IOMemoryDescriptor *memory;bool readOnly=true;
    SweetSurface():memory(new IOMemoryDescriptor(PAGE_SIZE)){liveSurface++;}
    void retain(){refs++;} void release(){if(!--refs){memory->release();liveSurface--;delete this;}}
};
static void *IOMallocZero(size_t n){return calloc(1,n);}
static void IOFree(void *p,size_t n){assert(n);free(p);}
static void flush_dcache64(uint64_t pa,unsigned n,int phys){assert(pa&&n==PAGE_SIZE&&phys);}
static uint64_t bluejay_gpu_map(const uint64_t *p,uint32_t n,bool ro,bool nx){
    assert(p&&n);mappedReadonly=ro;mappedNoexec=nx;if(failGPUMap)return 0;
    uint64_t va=nextVA;nextVA+=(uint64_t)(n+1)*PAGE_SIZE;return va;
}
static bool bluejay_gpu_unmap(uint64_t va,uint32_t n){assert(va&&n);return !failUnmap;}
static bool bluejay_gpu_submit(uint64_t job,bool fragment){assert(job);submissions++;submittedFragment=fragment;return !failJob;}
#define IOLog(...) ((void)0)
'''
cls=r'''
static uint64_t gpu_wired;
static unsigned gpu_submits;
class SweetGPUUserClient {
public:
    int fTask=1;
    SweetBO fBO[SWEET_GPU_MAX_HANDLES]={};
    IOReturn makeBO(uint64_t,uint32_t,SweetSurface *,uint64_t *);
    IOReturn dropBO(uint32_t);
    IOReturn run(const sweet_gpu_submit *,size_t);
};
'''
main=r'''
int main(){
    auto *uc=new SweetGPUUserClient;uint64_t out[4]={};
    assert(uc->makeBO(0,0,nullptr,out)==kIOReturnBadArgument);
    assert(uc->makeBO(SWEET_GPU_MAX_BO_BYTES+1,0,nullptr,out)==kIOReturnBadArgument);
    assert(uc->makeBO(1,4,nullptr,out)==kIOReturnBadArgument);
    gpu_wired=SWEET_GPU_MAX_WIRED_BYTES;assert(uc->makeBO(1,0,nullptr,out)==kIOReturnNoResources);gpu_wired=0;
    for(unsigned f=0;f<4;f++){
        failPrepare=f==0;failMapping=f==1;failSegment=f==2;failGPUMap=f==3;
        assert(uc->makeBO(1,0,nullptr,out)!=kIOReturnSuccess);
        assert(!liveMem&&!liveMap&&!prepared&&!gpu_wired);
        failPrepare=failMapping=failSegment=failGPUMap=false;
    }
    assert(uc->makeBO(1,SWEET_GPU_BO_NOEXEC,nullptr,out)==kIOReturnSuccess);
    uint32_t h=(uint32_t)out[0];assert(h==1&&out[3]==PAGE_SIZE&&gpu_wired==PAGE_SIZE);
    assert(mappedNoexec&&!mappedReadonly&&liveMem==1&&liveMap==1&&prepared==1);
    struct {sweet_gpu_submit req;uint32_t handles[2];} input={{out[1],0,1},{h,0}};
    assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnSuccess);assert(submissions==1&&!submittedFragment);
    input.req.requirements=1;assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnSuccess);assert(submittedFragment);
    input.req.job++;assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnBadArgument);input.req.job--;
    input.handles[0]=4096;assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnBadArgument);input.handles[0]=h;
    assert(uc->run(&input.req,sizeof(input.req))==kIOReturnBadArgument);
    input.req.requirements=2;assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnBadArgument);input.req.requirements=0;
    failJob=true;assert(uc->run(&input.req,sizeof(input.req)+4)==kIOReturnIOError);failJob=false;
    failUnmap=true;assert(uc->dropBO(h)==kIOReturnIOError);assert(liveMem==1&&liveMap==1&&prepared==1&&gpu_wired==PAGE_SIZE);
    failUnmap=false;assert(uc->dropBO(h)==kIOReturnSuccess);assert(uc->dropBO(h)==kIOReturnBadArgument);
    assert(!liveMem&&!liveMap&&!prepared&&!gpu_wired);
    auto *surface=new SweetSurface;
    assert(uc->makeBO(PAGE_SIZE,SWEET_GPU_BO_NOEXEC,surface,out)==kIOReturnSuccess);
    assert(mappedReadonly&&mappedNoexec&&!liveMap&&surface->refs==2);
    surface->release();assert(uc->dropBO((uint32_t)out[0])==kIOReturnSuccess);
    assert(!liveMem&&!liveMap&&!liveSurface&&!prepared&&!gpu_wired);
    delete uc;puts("PASS user-client bounds, submit routing, cleanup and quarantine (mock IOKit)");
}
'''
code=pre+'\n'+bo+'\n'+cls+'\nstatic void\n'+function('syncBO')+'\n'
for n in ['makeBO','dropBO','run']:
    code+='\nIOReturn\n'+function('SweetGPUUserClient::'+n)+'\n'
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.cpp').write_text(code+main)
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'kernel/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
