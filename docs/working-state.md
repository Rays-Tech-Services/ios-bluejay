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
