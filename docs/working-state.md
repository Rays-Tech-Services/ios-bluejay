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

User initially said Ethernet adapter Settings crashed, then clarified the page
eventually loaded. Do NOT classify as a proven Settings crash; may be slow.
It shows Configure IP Automatic, IP/subnet/router all blank. Thus previous
manual settings instructions were not applied, and DHCP has no lease.
Leave Automatic for the pending BPF fix build (commit4e33bb7). User still
has no confirmed web page load; "WAIT IT LOADED" referred to Settings.

BPF fix CI39540681694 (4e33bb7) succeeded; downloaded out/ci-usb-bpf.
Kernel SHA25638e71a7d98bcfe091b19caa4d083921239fb8eb047b31de35d6efffd486e40ab.
Packed out/phone-ios-usb-bpf/boot.img, 8 CPUs, sweetusb=2 sweetbattery=2
and sweetusblogip=0xc0a80201 (host sharing IP192.168.2.1). User returned
fastboot; advance notice given, temporary fastboot boot returnedOKAY.
UDP capture session77153 writes out/usb-bpf-live.log for5min. Awaiting
DHCP address, ping, and actual phone HTTP GET. Installed boot untouched.

BPF fix hardware success: phone now obtains192.168.2.2 automatically,
responds to ping (~1.7–2.7ms), sends >180KB of live logs from2.2 to2.1.
DHCP/address issue resolved. Native Ethernet visible; leave Automatic.
Need user retry fixed test http://192.168.2.1:8080/plain and then external
http://neverssl.com. No phone HTTP request yet seen in localserver log.
MobileSafari exec observed in new live log; no proven page rendering fault.
Do not call internet complete until real page received/displayed.

## WebKit rendering investigation (2026-10-21)

User now explicitly requests graphics/WebKit diagnosis. USB local HTTP transport
is verified: `out/pixel-web-test.log` records phone 192.168.2.2 GET /plain
HTTP 200 twice. The user sees a white content area, including after disabling
GPU Process: DOM Rendering and GPU Process: Canvas Rendering and restarting
Safari. Do not treat the white page as proof of failed networking. External
Internet access is separate and remains unverified by these local requests.

Earlier kernel log shows WebContent, Networking and GPU process launches; GPU
created IOSurface id 28, 1125x1536, format b3a8, two planes. Apple SDK defines
b3a8 as uncompressed RGB10 wide-range color plus a separate alpha8 plane.
Framebuffer currently copies every four-byte pixel as BGRA, including w30r
surfaces observed from SpringBoard. That is a concrete format-handling gap,
but is not yet established as the cause of blank WebKit content: WebKit's
surface may be composed into another surface before framebuffer presentation.
Do not claim a framebuffer conversion alone fixes WebKit.

Fresh UDP capture started at `out/webkit-render-live.log`, port 5140; asked
user to force-close/reopen Safari and load the local plain page during capture.
No new kernel or boot/partition changes made for this investigation yet.

WebKit test prepared in commit e52d455, CI run 39544599771. See
`docs/webkit-rendering.md`. Experimental `sweetweb_bgra=1` substitutes a fully
described BGRA allocation for parentless WebKit RGB10 surfaces before painting;
other processes unchanged. Plane-0 default BPE for b3a8 corrected to four.
Sanitized host geometry/process-selection checks pass. Hardware rendering
result still pending. User asked for fastboot readiness asynchronously; announce
immediately before any temporary boot. Never flash userdata for this test.

WebKit CI 39544599771 succeeded. Kernel SHA256
`ded7c2a36b247f409c015abeaa45aaf5bce120f42bd0c9e9cfe5de1d1e014ca9`.
Temporary image `out/phone-ios-webkit-bgra/boot.img`, SHA256
`4cb4e020b769a70ca54094c56efe208d00a2d91e684f64454f6f2d1108c7ee1d`.
Exact prior phone ADT, RAM and boot OS metadata retained; old command line plus
`sweetweb_bgra=1` verified. Structured state: out/webkit-bgra-test-state.json.
QEMU smoke running under snapshot with the same kernel; phone not booted yet.

WebKit kernel QEMU smoke passed: display driver started, SmokeRoot mounted,
launchd exec reached after 31 seconds. Expected errno 2 panic: the empty smoke
root has no launchd. This does not exercise WebKit itself. Temporary phone boot
and visible webpage rendering are still pending fastboot readiness.

Temporary WebKit BGRA image booted via fastboot successfully. User reports
"its black"; asked whether only webpage or entire display. Do not mark fixed.
Phone log out/webkit-bgra-phone.log confirms GPU process requests b3a8/w30r
normalized to BGRA with the intended geometry; subsequent local /plain GET
requests return 200. No WebKit crash observed in captured log. Full phone
screen state still needs user clarification. Installed boot unchanged.

User clarified BGRA test: only Safari webpage is black, controls remain
visible. WebKit rendering remains broken. User explored Linux/Mali backend,
then chose to continue software-rendering work first (WebKit, wallpaper/blur,
performance). No GPU driver or Linux boot has been attempted.

Commit bbc2dbc adds opt-in sweetweb_trace=1: bounded WebKit surface sampling
and map/export/lock/unlock events. No rendering/synchronization behavior changed.
CI 39548618591 in progress. Sanitized probe and format tests pass. Fastboot
readiness requested asynchronously; announce before any boot. Existing image
with BGRA policy alone remains out/phone-ios-webkit-bgra/boot.img.

User confirms phone is in fastboot and explicitly authorizes automatic
temporary boot once ready; no need to ask before temporary boots again.
Current diagnostic build still compiling. Preserve installed boot/userdata.

Read-only follow-up research: matching WebKit source defines
AcceleratedDrawingEnabled in WebPreferencesDebug.yaml, WebKit default true.
WebPreferencesCocoa.mm reads debug overrides from standardUserDefaults using
its globalDebugKeyPrefix. This is a candidate for explicit unaccelerated
bitmap drawing; no preference injection, binary patch, or root change has
been implemented. GPU process toggles alone are not equivalent to this flag.
Downloaded matching YAMLs in out/graphics-research/. Await trace evidence
before choosing another behavior change.

WebKit trace CI 39548618591 succeeded; kernel SHA256
354ab380d4efbc91811b56d47fcb2086d46574d02b0812fde0d8e78c72853324.
Image out/phone-ios-webkit-trace/boot.img SHA256
610f20e812a799083ca4eb29ce8b1fcc72285728fc9f9e21eb5f2fcc4271ea33.
QEMU display/root/launchd boot smoke passed in 31 seconds. Temporary fastboot
boot returned OKAY. Same phone ADT and old args plus sweetweb_trace=1 retained
and verified (including sweetweb_bgra=1). Capture out/webkit-trace-phone.log,
UDP5140 listener session44987 lasts15min. Requested user open /plain in Safari
and leave15seconds to collect buffer samples. Rendering result pending.

Trace result (fresh-eyes review 2026-10-21): every WebKit GPU-process surface
(13 ids) stayed all-zero for 30 polls after its single GPU lock/unlock; backboardd
mapped and read them. Tiles are never painted; compositor handoff works. WebKit
draws via CGIOSurfaceContext -> QuartzCore AccelRenderer (Metal fails -> SWContext).
Commit 4a91c55 adds opt-in `sweetweb_cpu=1`: refuse IOSurfaces for the WebKit
GPU/WebContent processes so WebKit's verified fallback uses CPU CGBitmapContext
buffers. Host format/probe tests pass. CI 39550812835. Packing recipe verified
byte-identical for the trace image: mkpayload KERNEL out/bluejay-wake.adt +
cmdline, mkbootimg shim/out/shim.o --like backup/stock/boot.img.
WebKit FIXED with sweetweb_cpu=1 (temporary boot out/phone-ios-webkit-cpu/boot.img,
SHA256 129acec5...4129): user confirms Safari draws the local test page. See
docs/webkit-rendering.md. Installed boot_a NOT updated; needs a decision to flash.

## Thermal governor and wallpaper flags (2026-10-21)

Source changes exported into `kernel/0001-bluejay-port.patch` and synchronized
with the local XNU checkout and `kernel/include/BluejayThermal.h`:

- Failed battery reads request the hot clock level instead of indefinitely
  retaining full-speed clocks with a stale temperature.
- Apply policy on the first sample; retry both clock domains after a partial
  ACPM failure, including when temperature returns to the previous level.
- Preserve both A76 and X1 boot-time clock ceilings.
- Check ACPM channel bounds before reading descriptors, require aligned queue
  data and valid ring indices, and order response reads before consuming them.

`tools/package-thermal-wallpaper.py` packages an explicitly supplied rebuilt
kernel using a previous phone boot image's ADT and flags. It replaces duplicate
or disabled options with `sweetbattery=2 sweetthermal=1 sweetfix_posterboard=1`,
checks the command-line limit, copies stock boot metadata with `--like`, and
records input and output SHA256 hashes. It requires a new output directory.
The existing PosterBoard workaround is specific to iOS 16.7.16 (20H392), checks
the expected instruction, and may skip a different build. Wallpaper recovery
is not yet verified.

After CI builds the updated patch, package with:

```sh
python3 tools/package-thermal-wallpaper.py \
  out/ci-thermal-wallpaper/kernel.development.sweet \
  out/phone-perf-7-x2/boot.img out/phone-thermal-wallpaper \
  --shim shim/out/shim.o --like backup/stock/boot.img
```

Validation: thermal and battery host checks compiled with warnings as errors
and ASan/UBSan passed; three flag packaging tests passed; reverse patch check
against local XNU and source whitespace check passed. The latest phone image's
updated argument line is 424 bytes (607 usable); saved for review in
`out/thermal-wallpaper-cmdline.txt`. No updated kernel or boot image was built:
full Xcode is unavailable locally, so CI build and QEMU smoke remain pending.
No phone boot, flash, or userdata change was performed. Phone clock transitions
and wallpaper behavior remain unverified. Give advance notice before a future
temporary boot so the user can record it.

### Thermal + PosterBoard temporary boot (2026-10-21)

CI initially exposed a pre-existing stray `void` before the ACPM helper
functions. Removed it in commit `a0d2023` (governor recovery / packaging changes
are in `fdb7a7f`). Corrected CI run 39562526847 succeeded:
https://github.com/Rays-Tech-Services/ios-bluejay/actions/runs/39562526847

- Kernel: `out/ci-thermal-wallpaper/kernel.development.sweet`, SHA256
  `addfd6471a782d591fa8fffbcbe633c90710c0d9a9e49399202b132e2d3d974a`.
- Image: `out/phone-thermal-wallpaper/boot.img`, SHA256
  `6d76d19b33bc8b0d3f3c816a8154f9759449d34c69b5bf9555ba688acadbb668`.
- Eight CPUs, `shimcpu=1`, `sweetbattery=2 sweetthermal=1
  sweetfix_posterboard=1`; previous phone ADT and remaining flags preserved.
- Input/output hashes and full flags: image directory `manifest.json`.
  Stock boot metadata copied using `--like backup/stock/boot.img` and checked.
- QEMU smoke passed diagnostics, EL2/EL1 transition, initrd handoff,
  framebuffer driver, virtio disk, userdata discovery, HFS root mounting,
  and launchd exec (empty smoke root). QEMU does not validate physical
  thermal hardware or wallpaper behavior.
- After advance recording notice, `fastboot boot` returned OKAY for send
  and boot (0.433 s total). No installed boot partition or userdata flashed.
  Phone UI, wallpaper recovery, and live thermal transitions await observation.

USB detection and QEMU local sockets required execution outside the sandbox;
empty sandboxed fastboot output did not mean the connected phone was absent.

## Display tearing: enable existing TE synchronization (2026-10-21)

User reports substantial tearing on the thermal + PosterBoard temporary image.
Its manifest confirms that `sweetfb_te` was absent, so the existing TE hold
path was disabled while `sweetfb_hz=60` copied into live scanout memory.
Local GS101 reference `research/display/samsung/cal_9845/regs-decon.h` and
`decon_reg.c` confirm GLOBAL_CON idle bit 5 and TRIG_CON mask bit 4 used by
the implementation. Enabling the path masks the panel trigger around the
entire multi-layer copy, waits for DECON idle, then orders framebuffer writes
before releasing the trigger. Actual hardware effectiveness is unverified.
The existing path counts a 20 ms idle timeout and still copies; logs must be
checked for timeouts before calling this a reliable tearing fix.

Updated `tools/package-thermal-wallpaper.py` to force `sweetfb_te=1` alongside
the governor and PosterBoard options, including replacement of disabled or
duplicate TE arguments. All three packaging tests pass. Prepared
`out/phone-thermal-wallpaper-te/boot.img`, SHA256
`6e04ec7c7778a02fbab1edad7675d7ef7428d12dd9613fea561bba34b70967ad`.
Verified byte-for-byte identical kernel payload and ADT to the previous
thermal + PosterBoard image, apart from the command-line field. Kernel CI and
QEMU evidence above therefore applies to the same kernel. No new kernel build
was necessary. Boot and visual verification await fastboot readiness.

TE image temporary boot: after user fastboot readiness and recording notice,
verified the manifest SHA256 and `sweetfb_te=1`, detected the expected Pixel,
and ran `fastboot boot out/phone-thermal-wallpaper-te/boot.img`. Sending and
booting both returned OKAY (0.440 s total). No partitions flashed. Awaiting
user observation of scrolling/animation tearing; hardware effectiveness and
TE idle timeout counters remain unverified.

## Restore native resolution (2026-10-21)

User requested reverting the earlier smaller rendering resolution. Updated
packaging to remove `sweetfb_lw=828 sweetfb_lh=1792` (including any other
explicit logical-size overrides) and force `sweetfb_native=1`. This selects
the panel's native 1080x2400 geometry instead of the smaller scaled output
or the default iPhone logical dimensions. Thermal, PosterBoard and TE flags
remain enabled. Packaging tests pass, including disabled-native and smaller
size override removal. Kernel and ADT bytes are unchanged.

Prepared `out/phone-native-te/boot.img`, SHA256
`20c5a6b105c3d33da9cae51d85678d8ab351f8e0ac374f4abea0f52274b21523`.
No phone boot or flash yet; fastboot currently detects no device. Awaiting
user fastboot readiness for temporary boot. Installed boot is unchanged.

Native-resolution temporary boot: user confirmed fastboot readiness. After
recording notice, verified image SHA256, `sweetfb_native=1`, retained TE flag,
and absence of logical-size overrides. Expected Pixel detected; temporary
`fastboot boot out/phone-native-te/boot.img` returned OKAY for send and boot
(0.408 s total). No partitions flashed. Native-resolution UI behavior and
tearing improvement still require phone observation.

## USB Ethernet reconnection check (2026-10-21)

User requested enabling USB Ethernet on the native-resolution boot. The image
already includes `sweetusb=2`. Read-only checks outside the sandbox after the
user reconnected the cable still found no Pixel CDC-ECM interface and no Pixel
in the Mac IOUSB device tree. Saved Sweet iOS network service is enabled, but
has no current Ethernet address. Internet Sharing preferences retain NAT=1
and destination en5, but com.apple.NetworkSharing is not running and bridge100
is absent. These saved settings do not prove an active USB or internet link.
No Mac network configuration changed. Requested fastboot readiness for a fresh
temporary boot with the cable attached to reinitialize USB; current cause of
missing USB enumeration is unverified.

## Battery read recovery prepared (2026-10-21)

User reports inaccurate battery again and has returned the phone to fastboot.
Prepared source changes: retry complete read transactions up to three times;
separate charger-status validity from fuel-gauge validity so a charger read
failure cannot freeze valid SOC/voltage/temperature; preserve last known
charging flags until charger reads recover; remove the synthetic 100% startup
capacity; mark failed gauge samples stale/invalid and expose failure count.
Charging controls and gauge calibration are not changed. This fixes identified
software reporting defects, but the cause on this particular boot is unverified.

Sanitized battery host checks cover advancing SOC during charger failure,
preserved charge state, recovery/unplug, and rejection of invalid gauge data.
Those checks, packaging tests, source whitespace, and reverse patch check pass.
Shared decoder copies synchronized. Review diff: `out/battery-recovery-review.patch`.
Native-resolution packaging and TE settings are retained.

Commit/push to main for the required CI build was rejected by automatic
approval review: externally visible repository changes were not explicitly
authorized. No commit or push executed for these changes. Full Xcode is not
available locally. Need explicit authorization before committing/pushing and
building; do not boot the older image as if it contains this fix.

### Battery recovery kernel booted and USB restored

User explicitly approved committing/pushing the prepared changes. Commit
`79ba99f` CI run 39675029858 succeeded:
https://github.com/Rays-Tech-Services/ios-bluejay/actions/runs/39675029858
Kernel SHA256 `7541c41bf023893968796f0db475aea4ca0ae4b48e29b05fa0f5d886b3a05ad4`.
Image `out/phone-battery-recovery/boot.img`, SHA256
`7b51fcb7d4df6da156a9da88a3911b4b0dd16f95fa4ddf42cd9c031d519337f5`.
Host checks and all QEMU smoke checks passed through HFS mount and launchd exec.
Recording notice given; fastboot send/boot returned OKAY (0.447 s). No flash.

Fresh boot enumerated USB Ethernet en5. Saved Mac Internet Sharing started
without changing its configuration: bridge100 192.168.2.1/24, member en5,
phone MAC learned. Received 568 UDP log packets from the phone, saved in
`out/battery-recovery-phone.log`. This verifies bidirectional device/host link
activity, but external internet access has not been newly tested.

Phone log confirms `MAX77759 reported SOC, 100% 4302mV external=1 charging=1`,
thermal sensor 22C and ACPM clock requests succeeding. No failed-sample messages
in this captured boot. This is a measured gauge report, not the removed startup
placeholder; gauge calibration/long-term accuracy is not verified. Phone UI
percentage, charging transitions and external networking await user observation.
Native display 1080x2400 and TE hold on are confirmed in the hardware log.

USB reachability verified: 3/3 ping replies from 192.168.2.2, zero loss,
average 1.371 ms. This is local-link evidence, not an external internet test.

## Reported UI boot failure after battery recovery

User reports that iOS does not finish booting. Prior fastboot OKAY, DHCP,
USB logs and ping evidence established a running kernel/userspace network,
not a usable lock screen. Do not call that a completed UI boot.
Additional live log `out/boot-failure-live.log` continues to receive data;
framebuffer statistics show zero presented frames, with backboardd repeatedly
looking for AppleM2ScalerCSCDriver. No kernel panic observed. PosterBoard
crashed in the earlier capture. Cause remains unverified.

Prepared one-variable display-mode recovery test:
`out/phone-boot-recovery/boot.img`, SHA256
`71888a18cca338961e0aee0f61594630dc02926d26aa3b58f2dd0d6a6358ae4e`.
Only `sweetfb_native=1` changed to 0; kernel and ADT bytes match the battery
recovery image. Original full-resolution 1125x2436 logical mode maps onto the
1080x2400 panel; the earlier reduced 828x1792 overrides remain absent. Battery,
USB, thermal, PosterBoard and TE options retained. This tests the hypothesis
that forcing true panel dimensions breaks the iOS compositor's display-mode
assumptions. Awaiting fastboot readiness; no recovery boot or flash performed.

## Stock apps root (2026-10-21)

Cause of "missing apps": the 32 removable stock apps (Calculator, Weather,
Notes, Maps, Mail, Music, Clock, Calendar, ...) ship in
`/private/var/staged_system_apps`; only DataMigration's SystemAppMigrator
installs them, and sweet-root-tweaks.py skips DataMigration. Wallpaper files
are identical to the IPSW (234 files); black wallpaper is the rendering issue.

`tools/install-staged-apps.py` moved them into `/Applications` (root:wheel,
original modes) in a clone of `out/root-aligned.img` -> `out/root-apps.img`
(raw SHA256 `f59bc576031fac7a2ddddfe67d95b6ff011c1e03b84f78817ff26e34451d22db`),
sparse `out/root-apps.simg` (SHA256 `4b5e319e672dfa514895cb76011271b5099330a36ec768cc4f9e3adcfdcad173`).
Manifest still 321301 entries (9416 rewritten).

QEMU (battery-recovery kernel, 1 CPU): fixer 321301 entries, 0 errors; lock
screen reached ~32 min after boot (first-boot LaunchServices rebuild blocks
SpringBoard in `_LSCopyServerStore` for ~20 min). IconState.plist lists the new
apps and widgets. Home screen not visually checked (no QEMU touch).
Phone not flashed yet: flashing wipes the user's current iOS data.

User authorized wipe. 19:34-19:40 flashed `out/root-apps.simg` to userdata
(90/90 chunks OKAY, 360 s), then temporary-booted `out/phone-fix/boot.img`
(OKAY). Fixer result on the phone screen awaited; boot_a not touched.

USB Ethernet on the new root (temporary boots only, boot_a unchanged =
pre-USB 8-core image, which is why normal power-on has no USB). 21:34 booted
`phone-ios-webkit-cpu` (129acec5): en5 + sharing up, phone answered only
192.168.7.2; configd ignored en0. 21:39 booted `out/phone-usb-log71/boot.img`
(same image, one byte: sweetusblogip=0xc0a80701 so logs reach 7.1 before DHCP;
SHA256 8d565352...). First boot after the wipe: addNetworkInterface runs, configd
picks en0 up only minutes later (same as the original usb-network boot).
21:46 user sees Ethernet in Settings; 192.168.2.2 replies 3/3 (~2.7 ms).
Log: out/usb-apps-live.log. External internet not yet verified.
External internet VERIFIED (IMG_5953): Safari loads https://en.wikipedia.org
over USB Ethernet + Mac Internet Sharing, with text and images rendered.

## Shutdown panic fix (2026-10-21)

iOS Shut Down panicked "Halt/Restart Timed Out" (default halt only parks the
CPU; PEHaltRestart's 30 s timer fires). Commit 9c14b70: bluejay power off =
PSCI SYSTEM_OFF (falls back to SYSTEM_RESET), restart = SYSTEM_RESET;
sweetreboot=1 unchanged. CI 39709118621 success, kernel SHA256
`0365ffa3428e7191b6a64e67b746c69f13dad56cede20b36c4532bc57a75f301` (out/ci-shutdown).
QEMU: fixer RB_HALT logs "power off -> PSCI SYSTEM_OFF" and the VM powers off,
no panic. Phone image `out/phone-ios-shutdown/boot.img` (SHA256 addd6993...e367):
webkit-cpu image's exact shim, ADT and cmdline (old payload reproduced byte-exact;
boot_v4 reproduced byte-exact), only the kernel payload replaced.
Phone behaviour (power off vs charger screen with cable) not yet verified.

PSCI SYSTEM_OFF did not power off the phone (still "Halt/Restart Timed Out";
Pixel EL3 parks the CPU). Commit 4c25079: power off clears PMU PS_HOLD_CONTROL
(0x17460000+0x3e9c) bit 0x100 via SMC 0x82000504 like Google's pixel-reboot
(research/gs101/dts/gs101.dtsi), then after 3 s resets into the bootloader;
restart = PSCI SYSTEM_RESET then SWRESET (+0x3a00 bit 0x2). CI 39710710573,
kernel SHA256 a96e3e545235bf10a3029b10a347263ff759f465f6979533a141e6725c7aff8f.
QEMU: PMU read unsupported -> fallback reset, no panic.

INSTALLED: `out/phone-ios-pshold/boot.img` (SHA256 b92a5d89...c04b; webkit-cpu
image + this kernel; USB, battery, sweetweb_cpu) flashed to boot_a 22:28 at
user request. Flash cleared slot-successful:a; shimmarka (out/phone-mark-success)
re-set it: fastboot shows successful yes, unbootable no, retry 3. Previous
installed image backup: backup/known-working-ios-8core-2026-10-20/boot.img.
Normal reboot issued 22:3x. Phone shutdown behaviour not yet verified.
Shut Down VERIFIED on the phone (user, 22:4x): installed PS_HOLD kernel powers
the phone off; no "Halt/Restart Timed Out" panic.


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

Physical GPU update: direct-MMIO kernel fa085af, CI 39730945390, SHA256
`3b223959da448a903f7aea70e4345d9495928f9a9b4197e0d623b27a571df50c`,
passed QEMU smoke and real level-1 identification (GPU_ID 0x92020010, 20 shaders).
Ray confirmed iOS lock/home screen. Level-2 temporary image SHA256
`b1b5e576bb9a004fb8647a6e8a44414a0a0bd019ac2e49fbb0b35b15a5cd07fb`
reset successfully; L2 and tiler powered on, shaders timed out at zero.
Level 3 withheld. Manual core-stack fallback is queued for CI/QEMU/device
validation; seven sanitized mocked power cases and 20 probe gate cases pass.
No partitions flashed; GPU acceleration is not yet working. Details and
log paths are in docs/gpu-bringup.md.

Renderer-enabled candidate ready (not deployed): kernel e9cd287 / CI 39735597187
build passed, SHA256 `f5e584200ebbd728d656138d21b78cf2c08041b5edc543a6773b63b1b7b49918`.
All QEMU smoke checks passed, including level-4 emulated-board guard.
Panfrost library d89bff4 / CI 39736044323 built/signed, SHA256
`4b5f1cc84a2c93a31850dfbd04f5b70f769f9d0ee4a39cb84963f613c67e9947`.
Phone image out/phone-gpu-renderer/boot.img SHA256
`0e7f3d075af9eaaa86868c67c1b3c5cf90330285bfc977298d4f820bece56ad1`
requests sweetgpu=4, backboardd injection and triangle self-test; proven shim
and stock metadata preserved. Final root-gpu.simg expands to 16GiB, verified
raw SHA256 `fa741e1159d86cceb6e703d422c0265bbb3fe8c406814cc287274df7c66ffebb`,
sparse SHA256 `ea36cc0bda17514af444da256ad8c30ad3030c3703f58f204d55792c3c768b17`.
Root includes the dylib and 48 checked PNG wallpaper layers. Full deployment
manifest is out/gpu-deployment.json. Host checks include 23 probe gates,
7 power cases, VM/job permissions and fault quarantine, and mocked IOKit
buffer lifecycle/submission cases. Physical rendering remains unverified.
Ray requested the next normal iOS boot attempt full acceleration. Do not
perform another diagnostic-only phone boot. Userdata installation requires
new explicit wipe approval and one maintenance fixer boot before the normal
renderer-enabled boot. No partition has been flashed; boot_a remains unchanged.

GPU-enabled install (2026-10-22, physical phone): Ray approved the userdata
wipe. out/root-gpu.simg (sparse SHA256 `ea36cc0b…8b17`) flashed with
`fastboot -S 128M`, 91/91 chunks OKAY, 365 s (out/gpu-userdata-flash.log).
Fixer boot out/phone-fix/boot.img (SHA256 `0bf55cf0…841a`) returned to
fastboot after ~7.5 min. Temporary boot of out/phone-gpu-renderer/boot.img
(`0e7f3d07…6ad1`); boot_a unchanged. First UDP log ~5 min after boot
(out/gpu-renderer-phone.log). Result: PD_G3D on, GPU_ID 0x92020010, reset ok,
L2 and tiler powered, **manual stack power TIMEOUT and shader TIMEOUT**
(JM 0xf0000, stack ready 0x0, trans 0x7f, shader trans 0x779fff).
SweetGPU open from backboardd returns 0xe00002d8 (not ready); sweetgl
falls back to Apple's EAGL ("no Mesa context"), and its texture fixes were
not installed ("unexpected QuartzCore"). SpringBoard exec'd. GPU acceleration
is still not working; next step is the shader-core power path (core stacks
transition but never report ready).
Follow-up: the phone stayed on the Apple logo. iOS was up (touch events,
daemons running) but nothing was drawn after the boot frame: sweetgl retried
the SweetGPU open 43 times and every EAGL context fell back to Apple's EAGL,
which cannot work without Apple's GPU driver, so SpringBoard never got a
display. sweetgl must fall back to CPU rendering (not Apple's EAGL) when
SweetGPU is unavailable. Ray: no further iOS boots until the GPU works.
Root cause of the frozen display: every EAGLContext init reopened SweetGPU,
and newUserClient waits up to 5 s for a self-test that had already failed,
so backboardd's render loop blocked on every frame. Fix (uncommitted, not yet
built in CI): sweet_drm_open treats a failed open as final, sweetgl_init
treats a failed Panfrost screen as final, and backboardd's load hook skips
the EAGL shim entirely when SweetGPU cannot be opened (same approach as
HackiOS inject.c opengl_es()/my_renderer). Host test tools/sweetgl/drm-test.c
covers the single-open rule and passes with ASan/UBSan. The shader-core power
failure is unchanged; this only stops a dead GPU from freezing the UI.

GPU clock query prepared (2026-10-22): shim/gpu-query.h adds `shimgpuq`, a
read-only diagnostic screen: PD_G3D / PD_EMBEDDED_G3D config+status and ACPM
DVFS FREQ_GET for domains 0-13 (mainline gs101 order; G3D=5, G3DL2=6 to be
confirmed by plausible kHz). No writes. Image out/phone-gpu-query/boot.img
SHA256 `a6ee63f62407e183e153ce33cd730431ddc499b58dc3da1092d4159eaf9bd49b`
(stock metadata via --like). QEMU (out/q-gpu-query): screen renders, every
query returns error 1 (no ACPM firmware), shim halts. Phone run pending.
Hypothesis under test (unverified): the shader clock domain G3D is stopped,
which would explain L2/tiler ready but shader power-on timing out.
