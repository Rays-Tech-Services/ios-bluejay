# Working Pixel state (2026-10-20)

Real phone: iOS reaches the lock screen; user confirms touch, navigation,
animations and app interaction. Eight-core image is much faster than one,
two and four cores. User estimates at least 12 fps; no FPS counter measured it.

## Images and backups

- Normal eight-core image: `out/phone-ios-8cpu/boot.img`.
- Verified read-only backup: `backup/known-working-ios-8core-2026-10-20/boot.img`.
  SHA256: `40ec13a6885a86542a08101154a75b3fd50d87d307a1bfd1a74e4e459c996c03`.
- Full installation artifact backup (not a live phone data backup):
  `backup/known-working-ios-1core-2026-10-20/`.
- Installed root: `out/root-aligned.simg`. Fixer completed 321301 entries,
  zero errors (IMG_5914). Its static entry aligns SP before C. The upstream
  fixer failed with EXC_ARM_SP_ALIGN 0x103 (IMG_5912).
- Do not reflash userdata while diagnosing graphics or boot persistence;
  it contains the user's current iOS settings and changes.

## Current task and scope

User explicitly deferred graphics diagnosis. Blue-to-white rendering and black
wallpaper remain unresolved. Focus is automatic boot and Settings persistence.
The root is writable (`ufsrw=1 rootrw=1`); a specific Settings change still needs
an end-to-end reboot test. Do not reflash userdata.

## Permanent boot: installed and slot success verified

The backed-up eight-core image above was flashed successfully to boot_a.
Live DevInfo was inspected at LUN 3 LBA 6, magic DEVI, version 3.15.
The guarded one-time `shimmarka` helper backed up the original 4096-byte block
to userdata slack LBA 0x1dbfff9, outside HFS and the log ring, and verified it.
It changed only slot A's successful bit: word 0x00000c03 -> 0x00000e03.
Full-block readback verified all other bytes unchanged and storage flushed
(IMG_5926). Fastboot independently confirmed current-slot a,
slot-successful:a yes, slot-unbootable:a no, retry-count:a 3.
Audit: `backup/boot-success-update-2026-10-20.json`.

Normal `fastboot reboot` returned OKAY after advance recording notice.
User confirmed this normal installed boot reached the iOS lock screen.
A complete power-cycle and a selected Settings persistence test remain unverified.
Reflashing boot may clear the success flag again; check it after future flashes.

Fastboot refuses fetch of boot_a, boot_b and devinfo. Zero-byte files from those
failed attempts are NOT backups. Factory boot is in `backup/stock/boot.img`.
The DevInfo original backup is on phone storage, not yet exported to the Mac.

User requests advance notice before boots to record them.

## USB internet and battery work (active)

User requested both. See `docs/network-battery.md`. Diagnostic built at
`out/phone-power-usb/boot.img`; QEMU skips hardware correctly. Awaiting user
fastboot readiness before temporary diagnostic boot; give advance notice.
Kernel source now has opt-in sweetbattery=2 and sweetusb=2 Pixel paths.
Decoder tests pass; CI run 39532556491 successfully built commit 3bf96f0.
https://github.com/Rays-Tech-Services/ios-bluejay/actions/runs/39532556491
Kernel SHA256 e91e24b60c310b3e9041bf75a2d960bfe17671b17bf7fe36c191643395b553be.
Test image: out/phone-ios-usb-battery/boot.img (8 CPUs, sweetusb=2 sweetbattery=2).
First temporary observer boot succeeded but guard skipped the real phone:
Google vendor DT identifies google,GS101 BLUEJAY (not google,bluejay).
Corrected observer recognizes that identity and requires actual USB/UFS nodes;
QEMU still skips correctly. Corrected observer temporarily booted successfully; awaiting photo.
Kernel QEMU smoke with sweetusb=2 sweetbattery=2 reached test HFS root and
launchd exec; expected errno 2 panic because the smoke disk has no launchd.
Hardware battery readings, USB enumeration and internet are not verified.
No installed boot partition or userdata changes made for this work.

IMG_5930 observer: real battery register reads valid (~98.5%, 4448mV, 22C),
charging active. DWC3.1 ID 0x33313130, VER_NUMBER 0x3138302a. Corrected
kernel ID guard (old test kernel rejects this ID without starting USB).
Need new CI artifact before iOS USB/battery hardware test; do not use old
USB test kernel expecting enumeration. Photo contains raw observations in
`out/usb-battery-test-state.json` (local). No installed image changed.

User returned phone to fastboot for combined iOS test. Corrected DWC3.1 CI
run 39533700976 (commit 9fc2c82) is in progress; fetch worker session 20813
will download to out/ci-usb-battery-v2 after success. Do not boot the stale
combined test image until repacked with this new artifact.

Corrected kernel CI 39533700976 succeeded. SHA256
cd93feacaa32f774814984f62974ae3a9da8a55c4159f798c070916dbb22973d.
Downloaded out/ci-usb-battery-v2, packed out/phone-ios-usb-battery-v2/boot.img.
Temporary fastboot boot returned OKAY; testing eight cores with sweetusb=2
and sweetbattery=2. No installed boot/root partitions flashed. Awaiting iOS
battery result and Mac USB enumeration.

Combined temporary boot: USB enumerated correctly on Mac, AppleUserECMData
and AppleUserECM matched and en5 appeared. networksetup permanent host MAC
correct; ifconfig current MAC temporarily zero-like. Helper now handles this.
Set only new Sweet iOS USB service to manual 192.168.7.1/24, no router.
Then USB device disappeared. Ping failed and UDP log bind found no local
address. Async question asks whether user unplugged, iOS running connected,
or phone crashed. Need answer/photo before blaming driver. Battery UI not
yet confirmed. No internet sharing, forwarding, NAT, DNS configured.

User confirmed 99% and charging/unplugged changes in iOS. USB disappeared
because they unplugged; replug failed. Fresh v2 temporary boot: 3/3 ping
succeeded and live UDP logs captured. User says no Ethernet in Settings.
Preparing IOKit interface discovery bridge / en0 for native iOS DHCP and
DWC3 active-transfer / EP0 reset handling. Mac Internet Sharing still off.

CI 39536976696 (91d3286) succeeded for IOKit discovery / reconnect attempt.
Kernel out/ci-usb-network/kernel.development.sweet SHA256
c579df292c80f8de9caa12f88d90e265fe37899e593c2071d9374778a569c531.
Packed out/phone-ios-usb-network/boot.img (8 CPUs, same writable iOS flags,
sweetusb=2 sweetbattery=2). QEMU passed through HFS root to launchd exec
(expected missing-launchd errno2 on smoke disk). Not yet booted on phone.

Mac System Settings CUA failed: getApp opened Keyboard; General selected,
then Sharing click failed "Computer Use is not active" after long delays.
Do not keep retrying slow UI calls blindly. No Internet Sharing setting changed
through CUA. User asked via async to manually enable Wi-Fi sharing to ONLY
Sweet iOS (USB Ethernet). At last check bridge100 absent and InternetSharing
service absent. Existing net.inet.ip.forwarding=1 was observed, not changed
by us. Need enable sharing, then ask user for fastboot for new kernel.

Read-only local root image inspection was detached (/dev/disk4). The iOS cache
has addNetworkInterface -x -s with IOKit launch event and controller allowlist
including IOUserNetworkEthernet. New discovery adapter targets this.
Packet proof is local only: no default route/DNS or internet verified yet.
Live capture received 196212 bytes (out/usb-battery-live.log, private).

User said sharing setup done and phone was in fastboot. Temporarily booted
out/phone-ios-usb-network/boot.img successfully. USB came up after ~1 minute;
en5 static 192.168.7.1 applied; 3/3 ping to 192.168.7.2, avg1.612ms.
Live out/usb-network-live.log shows exec configd and exec addNetworkInterface:
new IOKit discovery triggers Apple's native setup job. Native DHCP/routing
completion not confirmed yet. Need ask whether Ethernet now appears in iOS.

Mac version27.0.1. Correct daemon label is com.apple.NetworkSharing (NOT
com.apple.InternetSharing). launchctl print shows not running, activecount0,
runs0. com.apple.nat.plist NAT Enabled=0 and SharingDevices=[], no bridge100,
bootpd DHCP off. Thus sharing not enabled despite user's "done"; need user's
Sharing panel screenshot or actual On switch + Sweet iOS destination selection.
CUA previously very slow/failed; avoid blind retries. tcpdump BPF denied;
sudo requires password; no root bypass attempted. net.inet.ip.forwarding=1
is pre-existing observed state. Installed working boot remains unchanged.

User subsequently actually enabled sharing. Confirmed com.apple.NetworkSharing
running, NAT Enabled=1, SharingDevices=['en5']; bridge100 IPv4 192.168.2.1/24,
member en5, phone MAC 02:00:5e:77:ee:02 learned on that bridge. User now sees
Ethernet in iOS Settings (native discovery goal achieved). Async asks adapter
Configure IP / IP Address / Router values. Need route/DNS/address validation.
After Mac bridging starts old 192.168.7.2 no longer pings; 192.168.2.2 also
not yet responding. Do not infer a DHCP lease. No internet verification yet.
Live log receiver session91202 appends out/usb-network-live.log; fixed target
192.168.7.1 may become unreachable when iOS moves to shared subnet2, so lack
of future UDP logs may be routing change. Mac iOS service still configured
manual7 but InternetSharing bridge owns subnet2; avoid changing Wi-Fi.

User set manual Ethernet settings and reports website connects but page stays
white. Internet HTTP success not independently proven: ping192.168.2.2 failed.
Mac sharing still running bridge100192.168.2.1; en5 is bridge member with no
IPv4 address (old7 UDP log target unreachable). Thus existing phone log is
stale and cannot diagnose current WebKit state.
Started fixed-content-only HTTP diagnostic tools/pixel-web-test.py on
192.168.2.1:8080, server exec session79133, logs out/pixel-web-test.log.
GET / HTML text only; GET /plain text/plain; no workspace file serving,
only subnet192.168.2 clients allowed. Mac self-test2.1 HTTP200304bytes;
firewall currently disabled (observed, not changed). Need user navigate
http://192.168.2.1:8080/plain then inspect log for peer other than2.1.
This separates completed network transfer from Safari/WebKit white rendering.
No Safari/WebKit crash in old log, which ended before browser test. Don't
assume missing GPU is established cause. No other graphics changes made.

User clarifies plain test Safari title says Can't Open Page. No GET from
phone in test server logs (only self-tests from2.1), so this is network load
failure, not established rendering bug. Sharing active and USBpresent but
phone2.2 doesn'tping. Requested photo ConfigureIP fields to verify settings.
Found concrete missing BPF plumbing in sweet_usb.c: no bpfattach and no
bpf_tap_in/out. Other XNU Ethernet drivers explicitly attach; bpf_setif
returns ENXIO when no interface registration. IPConfiguration uses this for
DHCP/ARP, explaining kernelping working but user-configured IP not applying.
Added attach and rx/tx taps (same header-stripping convention as if_fake.c).
Added sweetusblogip bootarg to select192.168.2.1 (0xc0a80201) for shared
subnet and retained unsent log cursor until a route becomes ready.
Next kernel CI needed. No HTTP internet success established yet.


## GPU handoff execution (2026-10-22)

Followed docs/codex-handoff.md. GPU self-test commit 6758b5b CI 39720181424
succeeded; kernel SHA256
4cbde719590bdc8c8e9ffb99df0ba93dca18a6a23ab3e2a09769dd8483601865.
All QEMU smoke checks passed through root mount and launchd exec.
Temporary phone boot out/phone-gpu1/boot.img (SHA256
c988a763a0d447e8965ead26005cbcf1a30bfa81c073134eb5a80997d71322bb)
succeeded after recording notice; old payload/boot reproduced byte-exact,
known-good shim and ADT retained. User reported phone booting. USB Ethernet
and live userspace logs verified in out/gpu1-phone.log, but early GPU output
was missed. GPU identity/power/job execution are NOT yet verified. No flash.

Applied and exported ios-sailfish patches 0104/0163/0139, credited joranv1:
9379db4 pushed. CI 39721582704 was cancelled by the workflow's concurrency
rule when 56100c6 superseded it; cancellation is not a compilation result.
56100c6 adds bounded sweetgpu_delay=0..300 seconds so logging can be ready
before the one-shot probe. CI run 39722154343 failed because the new sleep
calls lacked kern/sched_prim.h; 4107316 adds that header. Replacement CI
39723013130 passed. Kernel SHA256:
00e4ebd5ced81f373cc50e133c3674288188d95e4711e0d7c66628fa9610f426.
All QEMU smoke checks passed (out/smoke-gpu-delay-fixed). Temporary delayed
level-1 image out/phone-gpu1-delay/boot.img SHA256:
435011a42a3718277ff7607ce754030559b088de4874f9c3ff3b52145310c373.
Fastboot send/boot succeeded after recording notice. USB logs confirm the
new command line, display and touch, but no GPU output after the delay.
GPU identity and power remain unverified. 8e3a4e1 adds startup/access
checkpoints and uses the verified shutdown path's firmware PMU read ABI;
CI 39725175947 passed (kernel SHA256
f8470601ee348db4639c3e4df7bca9182d88bf5a2c3e641895c01897d0334c69).
QEMU smoke and explicit GPU emulation-guard checks passed. Temporary
phone-gpu1-smc image dcb845ae7efe40873e77b101ac4eb408e7699f2b2b3e04a26d7baf2a53db35a7
sent/booted OKAY; its capture again missed early checkpoints.
3e39487 adds bounded opt-in retained USB boot-log replay. CI 39727087656
passed; kernel SHA256 6224d7df80f8d77388a240d901007168de1f4146f66b12cbf7c963a2cb160292.
QEMU boot and GPU guard checks passed (out/smoke-gpu-replay). Temporary image
out/phone-gpu1-replay/boot.img SHA256
6b007fc30261d5d0d8dff1f216a06b097681bf166f40117897245af0fde26895
sent/booted OKAY after recording notice, with 30-second probe delay and six
replays. After an initially empty capture, out/gpu1-replay-phone.log recovered
825166 bytes: probe requested/started, config SMC returned 1, both status SMC
reads returned -EINVAL (-22), so the guard correctly avoided GPU access.
The firmware path added for status was incorrect: Linux reads PMU through
MMIO because SMC reads support only writable registers. fa085af restores
direct reads with individual checkpoints; CI 39730945390 is in progress.
Twenty ASan/UBSan host cases of the actual probe gate logic pass via
tools/gpu-probe-test.py (mock hardware). Log also confirms userspace/touch
and a SpringBoard-initiated shutdown; phone later detected in fastboot.
No failed-build image was booted, no partition flashed.

dc05b26 adds draft Panfrost sweetgl, explicit user-client ABI and macOS iOS
build CI. Host ASan/UBSan DRM checks, shell/Python syntax and Mesa patch
synchronization pass. Kernel SweetGPU service, successful iOS dylib build and
physical GPU execution remain pending. CI 39730620703 failed on missing native
shim includes (fixed 5be060d); 39731037894 failed on sched_setscheduler (Darwin
fix 3cce5ab). Replacement sweetgl CI 39731555134 is in progress.
Detailed evidence: docs/gpu-bringup.md.
Reproducible sanitized host checks: python3 tools/kernel-policy-test.py
passes all three test groups (selection/bounds, argument-space/string-copy
errors, AMFI policy/copy errors). Source whitespace and exported patch
reverse-application checks pass. Full IOSurface review and next gates are
in docs/gpu-bringup.md. SweetGPU, Panfrost sweetgl and root deployment remain
pending; the existing userdata-wipe prohibition remains in effect.
