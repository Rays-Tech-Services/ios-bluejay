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

CI run `39722154343` failed to compile the delay change because the scheduler
declarations were not included. Commit `4107316` adds `kern/sched_prim.h`;
replacement CI run `39723013130` passed. Its kernel SHA256 is
`00e4ebd5ced81f373cc50e133c3674288188d95e4711e0d7c66628fa9610f426`.
All QEMU smoke checks passed (`out/smoke-gpu-delay-fixed/`). Temporary image
`out/phone-gpu1-delay/boot.img`, SHA256
`435011a42a3718277ff7607ce754030559b088de4874f9c3ff3b52145310c373`,
booted successfully with recording notice and `sweetgpu_delay=120`.
Live USB logs in `out/gpu1-delay-phone.log` confirm the new command line,
display service and touch input, but contain no PMU/GPU probe messages even
after the delay. This does not prove a GPU power state or explain whether
the probe skipped or stalled. No failed-build image was packaged or booted.

Commit `8e3a4e1` adds probe-start, guard, thread, delay and access-boundary
checkpoints, and uses the firmware PMU read ABI from the verified PS_HOLD
path instead of direct PMU MMIO. GPU registers remain guarded by a successful
PMU response and the domain-on status bit. CI run `39725175947` passed;
kernel SHA256 `f8470601ee348db4639c3e4df7bca9182d88bf5a2c3e641895c01897d0334c69`.
All QEMU smoke checks passed with `sweetgpu=3`, including an explicit logged
skip before any PMU access (`out/smoke-gpu-smc/`). Temporary level-1 image
`out/phone-gpu1-smc/boot.img`, SHA256
`dcb845ae7efe40873e77b101ac4eb408e7699f2b2b3e04a26d7baf2a53db35a7`,
was sent and booted successfully. Its USB capture again started after early
startup messages; GPU execution remains unverified.

Commit `3e39487` adds opt-in `sweetusblogreplay=1..6`, replaying the retained
ring at ten-second intervals after USB configuration. This handles UDP sends
accepted before the host's interface/route is ready. Default logging remains
unchanged. Mac interface-selection host tests passed. CI `39727087656` passed;
kernel SHA256 `6224d7df80f8d77388a240d901007168de1f4146f66b12cbf7c963a2cb160292`.
QEMU boot and explicit GPU-guard checks passed (`out/smoke-gpu-replay/`).
Temporary image `out/phone-gpu1-replay/boot.img`, SHA256
`6b007fc30261d5d0d8dff1f216a06b097681bf166f40117897245af0fde26895`,
uses a 30-second delay and six boot-log replays. Fastboot send and boot both
returned OKAY after recording notice. The capture was initially empty after
user pause/continue, then recovered 825166 bytes including the early probe:

```
BLUEJAY GPU: requested 1 level 1
BLUEJAY GPU: start level 1 thread result 0
BLUEJAY GPU: starting level 1; reading PMU through firmware
BLUEJAY GPU: PD_G3D config SMC 0x1
BLUEJAY GPU: PD_G3D status SMC 0xffffffffffffffea
BLUEJAY GPU: G3D cores status SMC 0xffffffffffffffea
BLUEJAY GPU: firmware PMU read failed; GPU not touched
```

The probe did run; the firmware status-read path was wrong. Linux's
[Tensor PMU implementation](https://github.com/torvalds/linux/blob/master/drivers/soc/samsung/exynos-pmu.c)
uses MMIO reads because the SMC interface reads only writable registers.
Commit `fa085af` restores direct PMU reads with per-read checkpoints while
retaining replay and the domain-on guard. CI run `39730945390` passed.
Kernel SHA256 is `3b223959da448a903f7aea70e4345d9495928f9a9b4197e0d623b27a571df50c`;
all QEMU smoke checks passed (`out/smoke-gpu-mmio/`).
`python3 tools/gpu-probe-test.py` passes 20 ASan/UBSan cases for the actual
probe control flow, including board/QEMU guards, power-off/invalid status,
mapping/start failures, levels and delay bounds. Hardware operations are mocked.

The phone log also contains userspace/touch activity and a shutdown initiated
by SpringBoard. The phone subsequently appeared in fastboot. This is not GPU
execution or visual-compositor evidence; levels 2/3 remain pending.

## sweetgl development

Commit `dc05b26` adds the Panfrost user-space bridge, a versioned SweetGPU ABI
contract, the reused EAGL/dispatch/layer helpers, the G78 model patch and a
macOS iOS-build workflow. Sources and credits are in `tools/sweetgl/README.md`.
The draft translates parameter queries, create/free, mmap offsets, IOSurface
imports, synchronous submissions, waits, madvise and same-process FD proxies.
The kernel SweetGPU service is not implemented yet, so this is not a working
renderer. Prime proxies are valid only in-process while the BO remains alive.

Strict host compilation and ASan/UBSan checks pass for buffer bounds, stale
handles, submission failures and FD-proxy identity. Shell syntax, dispatch
generator syntax and Mesa patch synchronization checks pass. Full iOS build
validation is running in `sweetgl-bluejay`; device injection/rendering remains
pending. The first CI run `39730620703` reached native tools then failed to
find xf86drm.h; `5be060d` adds the native include path. Run `39731037894` then
failed on Linux-only sched_setscheduler; `3cce5ab` adapts it to Darwin pthread
scheduling. Replacement run `39731555134` is in progress. The new Mesa checkout
is pinned rather than following a moving branch.

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
SweetGPU and physical Panfrost/sweetgl validation are still pending. Root deployment requires a
new explicit userdata-wipe approval, as specified in the handoff.

## Direct-MMIO phone results

The level-1 temporary image `out/phone-gpu1-mmio/boot.img`
(SHA256 `fbfccf861e7c291a24e1f9ae776eef7165f611c861e04fbf4793a379850dda23`)
passed the physical identification test. PD_G3D configuration/status and core
status each read 1. GPU_ID is `0x92020010`; shader-present mask `0x779fff`
contains 20 cores, AS-present is `0xff`, and JS-present is `0x7`. L2/shader/tiler
ready masks were zero before the power-up test. Evidence is retained in
`out/gpu1-mmio-phone.log` and `out/gpu1-mmio-result.json`. Ray confirmed the
iOS lock/home screen. Identification does not establish accelerated rendering.

After recording notice, the same smoke-tested kernel was temporarily booted
with `sweetgpu=2`; image SHA256
`b1b5e576bb9a004fb8647a6e8a44414a0a0bd019ac2e49fbb0b35b15a5cd07fb`.
Fastboot send/boot returned OKAY. USB Ethernet re-enumerated and the log confirms
level 2 started. Reset completed, L2 and tiler powered on, but shaders timed
out with ready mask zero. Evidence: `out/gpu2-mmio-phone.log` and
`out/gpu2-mmio-result.json`. Level 3 was withheld. No partition was flashed.

The next diagnostic adds a bounded manual core-stack fallback after automatic
shader power-up fails, following Google kbase corestack_driver_control:
JM_CONFIG bit 8, power all present stacks, wait, then retry shaders. This is
an unverified hardware hypothesis. Seven sanitized host cases test normal
automatic operation, manual retry, stack timeout, failed L2, invalid/absent
stacks and failed resets; hardware is mocked. Source: Google GPU module
commit `20fff721667a227b3d6decf9dbc3798476390302`,
`mali_kbase/backend/gpu/mali_kbase_pm_driver.c`.

## Renderer-enabled draft

CI `39733290573` (commit `35905e5`) built and ad-hoc signed the arm64 iOS
Panfrost library successfully. Artifact `out/ci-sweetgl-v5/sweetgl/sweetgl.dylib`
SHA256: `30e27db78df22be92be79ef77b64c6e049d862db3094b77e145c70aa0d6cfc25`.
This is build evidence, not phone rendering evidence.

Ray requested the next phone boot attempt full acceleration. The opt-in
`sweetgpu=4` draft runs power-up and WRITE_VALUE before enabling the SweetGPU
user address space. The service exposes the shared ABI to administrator
clients and the entitled backboardd compositor (which runs as mobile), limits wired memory to 512MiB, preserves read-only IOSurface pages,
maps 16KiB host pages as 4KiB GPU PTEs, and adds a guard after each allocation.
Submissions are serialized and synchronous (fragment JS0, other JS1). A fault
disables submissions and attempts a bounded hard reset. If reset is unconfirmed,
client teardown retains wired buffers instead of freeing possible DMA targets.
The single GPU address space is experimental and restricted to administrators and the entitled
compositor; it does not provide per-client GPU isolation.

The draft also adapts ios-sailfish 0152/0157/0158 behavior: unsupported surface
wrap selectors fail; client-address images retain their cache mode and exact
row pitch, with read-only wiring fallback and balanced prepare/complete.
The backend host harness exercises exported code under ASan/UBSan with mocked
MMIO: parameters, invalid pages, PTE permissions/subpages, guard/reuse, slots,
timeouts, reset failure and quarantine. These tests cannot establish hardware
correctness. Kernel CI/QEMU, deployment and physical rendering remain pending.

CI `39734735027` compiled the driver but failed to link because importing
`device/device_types.h` ahead of IOKit gave io_object_t the IPC-port type in
the new user-client vtable. Match the existing IOSurface include order instead.
The next build also includes backboardd entitlement access, raw physical
segment queries, and client-image wired-budget accounting. Sanitized mock
IOKit tests cover allocation/prepare/map failures, invalid/stale handles,
submission bounds/routing, read-only imports, balanced cleanup and quarantine.

The undeployed local root clone contains the signed library and 48 PNG layers,
with dimensions checked against HEIC sources and fixer manifest entries added.
`out/root-gpu.simg` expands to 17179869184 bytes with independently verified
SHA256 `e8e91a93fb841ec1992916d28af35b878f2fb1a23f6e9f1179b59037bd723651`.
Installing it requires Ray's new explicit approval to wipe userdata.

## Full-acceleration install candidate

All QEMU smoke checks passed (`out/smoke-gpu-renderer/`): shim EL transition,
initrd payload discovery, framebuffer, virtio userdata, HFS+ mount and launchd
exec attempt. The level-4 GPU path explicitly skipped the emulated board
before MMIO. The empty smoke root's subsequent ENOENT panic is expected.

- Kernel commit `e9cd287`, CI `39735597187`: build passed; SHA256
  `f5e584200ebbd728d656138d21b78cf2c08041b5edc543a6773b63b1b7b49918`.
- Renderer commit `d89bff4`, CI `39736044323`: build/signature passed; SHA256
  `4b5f1cc84a2c93a31850dfbd04f5b70f769f9d0ee4a39cb84963f613c67e9947`.
  The imported CPU map now starts at the page base; the resource carries
  the intra-page pixel offset. A host case covers this.
- `out/phone-gpu-renderer/boot.img`, SHA256
  `0e7f3d075af9eaaa86868c67c1b3c5cf90330285bfc977298d4f820bece56ad1`,
  retains the proven shim/ADT/stock metadata and reproduces the old image
  byte-for-byte before replacing the kernel. It requests `sweetgpu=4`,
  signed-library injection into backboardd, triangle self-test and UDP replay.
- Final `out/root-gpu.simg` expands to 16GiB with independently verified SHA256
  `fa741e1159d86cceb6e703d422c0265bbb3fe8c406814cc287274df7c66ffebb`.
  This supersedes the intermediate v5-library image above. It includes the
  final dylib, 48 dimension-verified PNG layers and fixer ownership entries.
- `out/gpu-deployment.json` records complete hashes and inputs. The original
  root image, installed boot_a and phone userdata remain unchanged.

The reused QuartzCore shader-function patches have version guards. An offline
check of this root's dyld cache found the reference table sentinel differs,
so those optional shader-function fixes should skip; their visual effects
remain unverified. Trace-only address hooks are disabled. This candidate
attempts CoreAnimation GPU rendering but does not establish a usable compositor
until physical GPU writes, triangle pixels and lock/home interaction are seen.

Deployment is awaiting explicit userdata-wipe approval. It requires one
maintenance fixer boot to apply ownership, then the temporary renderer-enabled
iOS boot. Give recording notice before each. No boot_a flash is proposed.
