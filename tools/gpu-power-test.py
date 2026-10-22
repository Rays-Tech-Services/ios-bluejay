#!/usr/bin/env python3
"""Exercise the exported power sequence with mocked MMIO, not real hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
patch = (root / 'kernel/0001-bluejay-port.patch').read_text()
section = patch.split('diff --git a/bsd/dev/bluejay_gpu.c b/bsd/dev/bluejay_gpu.c\n', 1)[1].split('\ndiff --git ', 1)[0]
src = '\n'.join(s[1:] for s in section.splitlines() if s.startswith('+') and not s.startswith('+++'))
start = src.index('bg_power(void)')
brace = src.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (src[end] == '{') - (src[end] == '}')
    end += 1
const = '\n'.join(s for s in src.splitlines() if s.startswith('#define '))
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
static uint32_t regs[0x3000/4];
static bool reset_ok, l2_ok, auto_ok, stack_ok;
static unsigned shader_requests, stack_requests, hard_requests;
static uint32_t gpu_rd(uint32_t off) { assert(off < sizeof(regs)); return regs[off/4]; }
static void gpu_wr(uint32_t off, uint32_t value) {
    assert(off < sizeof(regs)); regs[off/4] = value;
    if (off == GPU_CMD && value == GPU_CMD_HARD_RESET) hard_requests++;
    if (off == STACK_PWRON_LO) { assert(regs[GPU_JM_CONFIG/4] & JM_MANUAL_POWER_CONTROL); stack_requests++; }
    if (off == SHADER_PWRON_LO) { shader_requests++; if(shader_requests == 2) assert(stack_requests && stack_ok); }
}
static bool gpu_poll(uint32_t off, uint32_t mask, uint32_t want, unsigned ms) {
    (void)mask; (void)want; assert(ms && ms <= 100);
    if (off == GPU_INT_RAWSTAT) return reset_ok;
    if (off == L2_READY_LO) return l2_ok;
    if (off == STACK_READY_LO) return stack_ok;
    if (off == SHADER_READY_LO) return auto_ok || (shader_requests == 2 && stack_ok);
    assert(off == TILER_READY_LO); return true;
}
'''
main = r'''
static void init(void) {
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);i++) regs[i]=0;
    reset_ok=l2_ok=auto_ok=stack_ok=true;
    shader_requests=stack_requests=hard_requests=0;
    regs[GPU_L2_PRESENT_LO/4]=1;regs[GPU_SHADER_PRESENT_LO/4]=0x779fff;
    regs[GPU_TILER_PRESENT_LO/4]=1;regs[GPU_STACK_PRESENT_LO/4]=0x7f;
}
int main(void) {
    init(); assert(bg_power()); assert(shader_requests==1 && !stack_requests);
    init(); auto_ok=false; assert(bg_power()); assert(shader_requests==2 && stack_requests==1);
    init(); auto_ok=stack_ok=false; assert(!bg_power()); assert(shader_requests==1 && stack_requests==1);
    init(); auto_ok=l2_ok=false; assert(!bg_power()); assert(!stack_requests);
    init(); auto_ok=false; regs[GPU_STACK_PRESENT_LO/4]=0; assert(!bg_power()); assert(!stack_requests);
    init(); auto_ok=false; regs[GPU_STACK_PRESENT_LO/4]=0xffffffff; assert(!bg_power()); assert(!stack_requests);
    init(); reset_ok=false; assert(!bg_power()); assert(hard_requests==1 && !shader_requests);
    puts("PASS 7 GPU power sequencing cases (mock hardware only)");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d); (p/'test.c').write_text('#include <stdint.h>\n'+const+'\n'+code+'\nstatic bool\n'+src[start:end]+'\n'+main)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,stdout=subprocess.PIPE)
print('PASS 7 sanitized GPU power sequencing cases (mock hardware only)')
