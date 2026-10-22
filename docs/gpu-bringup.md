# Mali-G78 bring-up

## Handoff execution, 2026-10-22

The hardware gate is a completed WRITE_VALUE self-test on the physical Pixel.
QEMU does not emulate this GPU. Do not treat a successful temporary image
transfer or an iOS boot as evidence of GPU execution.

Self-test kernel: commit `6758b5b`, CI run `39720181424`, SHA256
`4cbde719590bdc8c8e9ffb99df0ba93dca18a6a23ab3e2a09769dd8483601865`.
All `tools/qemu-smoke.sh` checks passed, including root mount and launchd exec.
The test root intentionally lacks launchd, so its subsequent ENOENT panic is
expected.

Temporary image `out/phone-gpu1/boot.img`, SHA256
`c988a763a0d447e8965ead26005cbcf1a30bfa81c073134eb5a80997d71322bb`,
uses the exact webkit-cpu shim and ADT, with the new kernel and `sweetgpu=1`.
The old payload and old Android boot image reproduced byte-for-byte before
replacement. Fastboot send and boot succeeded on serial `2B191JEGR04872`.
The user reported that iOS was booting. USB Ethernet subsequently enumerated
and live userspace logs arrived in `out/gpu1-phone.log`, but the capture
missed the early GPU diagnostic. GPU power, identity and job execution remain
unverified. No partition was flashed.

Commit `56100c6` adds `sweetgpu_delay=0..300` (seconds, default zero), sleeping
the diagnostic thread before any PMU/GPU access. Use a delayed level-1 test
after CI and QEMU verification so USB logging is reachable first. Step up to
levels 2 and 3 only after inspecting each preceding result.

## Environment and dyld milestone

Commit `9379db4` imports ios-sailfish patches 0104, 0163 and 0139 by joranv1,
with permission. `sweetenv` matches an executable basename and appends its
comma-separated environment entries; semicolons separate processes. The
built-in AMFI policy answers dyld syscall 0x5a with flags 0x7b and retains
ENOPOLICY for other calls. The source in `build/xnu` is exported to the tracked
kernel patch, with a passing reverse-application check and clean source
whitespace.

`python3 tools/kernel-policy-test.py` extracts the actual C functions from the
tracked patch and compiles sanitized host harnesses. Nine environment cases
passed (multiple processes and variables, exact basename matching,
malformed/empty process entries, oversized variable, absent spec, argument
space exhaustion and string-copy failure). AMFI checks
passed for supported/unsupported operations, failed copyin, null output and
failed copyout. These stubbed tests do not establish real dyld injection;
CI compilation, QEMU and later device injection remain separate evidence.

## IOSurface review of ios-sailfish 0152–0162

- 0152 explicitly rejects unsupported wrap selectors 6/7. This is relevant:
  returning success without a backing allocation can crash CA's GL renderer.
- 0153 and 0154 align explicit surface rows and the Pixel 1 shadow framebuffer
  to Adreno's requirements. The Pixel 6a already aligns default surface rows
  to 64 bytes. Preserve caller-owned row pitch and determine Panfrost's actual
  import requirements before changing explicit rows or display stride.
- 0157 adds IOSurfaceAddress-backed client pages, retaining the mapping's
  original cache mode and balancing prepare/complete. This is needed for
  zero-copy CA image imports or a deliberate userspace copying fallback.
- 0158 preserves read-only client pages in kernel/user mappings and falls
  back to a read-only descriptor. GPU import must preserve those permissions
  and reject writing jobs targeting read-only buffers.
- 0159 preserves the client's exact pitch; 0160/0162 refine explicit-row
  alignment. The final key-count heuristic is specific to observed sailfish
  callers, not a proven Mali or general IOSurface contract. Do not copy it
  without local evidence.
- 0155, 0156 and 0161 address Qualcomm SMMU faults, scratch padding and retry.
  Their hardware mechanisms do not apply to GS101 Mali's own MMU. Preserve
  the intent through bounded polling, fault reporting/reset, checked GPU VA
  mappings and buffer lifetime management in the new driver.

These review findings have not been applied as additional IOSurface changes.
SweetGPU/Panfrost and sweetgl are still pending. Root deployment requires a
new explicit userdata-wipe approval, as specified in the handoff.
