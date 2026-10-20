# Bringing it up on a real Pixel 6a

Real-phone status (2026-10-20): XNU has reached storage probing; the shim has verified display refresh, touch identification and finger coordinates, and a completed GPT header read. The successful storage sequence ungates clocks, exits Hibern8, clears device unit attention with REQUEST SENSE on device WLUN 0xd0, then sends START STOP UNIT with ACTIVE power condition (0x10). Each command uses task tag = slot and acknowledges old interrupt status before ringing the doorbell. This changes device power state but writes no logical blocks. The normal shim now performs the wake sequence before XNU. Kernel takeover of its own list and high-memory DMA still need hardware verification.

IMG_5906.HEIC: DEVICE ACTIVE 0; LUN 0 TUR 0; READ LBA1 0; words 0x20494645 / 0x54524150; DBR 0; OCS 0; response 0x01000021; response/status 0. This is a completed read, unlike IMG_5905's timed-out read with partial data. ABL interrupt aggregation was already 0, so disabling aggregation alone does not explain the change.

Steps 1 to 4 do not flash the phone. Installing the iOS root in step 5 erases Android userdata and requires explicit authorization before execution.


## 0. Before you start

* Unlocked bootloader (`fastboot flashing unlock`; this wipes the phone).
* Download the **factory image** for the exact build on your phone from Google. You need its `boot.img` for `--like`, and it is your way back to Android (`flash-all`).
* Know the way back to the bootloader: hold **Power + Volume down**.

## 1. Diagnostic boot (read-only)

```sh
shim/build.sh
python3 tools/mkbootimg.py shim/out/shim.o out/diag --like factory/boot.img
fastboot boot out/diag/boot.img
```

The shim draws one screen and then refreshes the button state forever. Take a photo of it and compare:

| Line | What you want | If not |
| --- | --- | --- |
| `entry EL` | `2` (`now EL 1`) | EL1 is fine as well; anything else means the entry code needs work |
| `MIDR` / `MPIDR` | `0x41..d05.` (A55), MPIDR `0x81000000` | |
| `CNTFRQ` | `0x01770000` (24.576 MHz) | if it differs, change `TIMEBASE` in `tools/mkadt.py` |
| `memory` | a range covering `0x880000000` + 3.5 GiB, and **no** `reserved ... overlaps` line in red | move `PHYS_BASE`/`MEM_SIZE` in `mkadt.py` and `shim/board.h` |
| `framebuffer` | `0xfac00000`. If the screen is black, the splash buffer is elsewhere | look for any address the FDT reports; if none, find the DECON window base. Set `FB` in `mkadt.py`, `board.h` and `SWEET_FB_PHYS` in the kernel patch |
| `DECON GLOBAL_CON / TRIG_CON` | `command mode, TE trigger unmasked` (or `TE trigger unmasked, TRIG_CON now ...`: the shim fixed it) or `video mode` | `SOFTWARE trigger`: the panel needs a kick per frame; add a `trigger` property to `sweet-fbcon` in `mkadt.py` |
| `touch` | `chip id (dummy, 36 48 ...)`, then `FTM5 answers; scanning on`; touching the screen changes `touch x/y` (0–1079, 0–2399) | `no FTM5 answer`: ABL left the touch supplies (PMIC LDO25M/26M) off. The kernel then needs PMIC access over ACPM (not written yet) |
| `watchdogs` | WTCON values; `was` with bit 5 set means ABL left one running | (the shim stops it either way) |
| `UFS` | `HCE 0x00000001`, `HCS` with bits 0–3 set, `DBR 0` | if HCE is 0, ABL shuts UFS down: the kernel needs a full Exynos UFS bring-up (not written) |
| `UFS vs TXPRDT/RXPRDT` | `0x0000000c` (4 KiB segments) | other values are fine; the driver always uses 4 KiB |
| `keys` | each line flips to `DOWN` while you hold that button | fix the bank/bit in `KEYS` (`mkadt.py`) and `key_dat`/`key_bit` (`board.h`) |

If the screen stays black but the phone doesn't reboot, the framebuffer address is wrong (step 2 helps). If it reboots after a few seconds, suspect the watchdog.

## 2. Optional: UART

A USB-C breakout on the SBU pins, 115200 8N1, 1.8 V levels (check your adapter). Enable the port in the bootloader (`fastboot oem uart enable`; this command is reported for Pixels and not verified here), then repeat step 1. When ABL's command line mentions `ttySAC` or `earlycon`, the shim copies everything it draws to the UART. Later it also passes `sweetuart=1`, so the kernel log follows.

## 3. Build the kernel

Push this repo to GitHub and run the **xnu-bluejay** workflow (Actions tab). It downloads xnu-8796.141.3, applies ios-sweet's 0001–0081, the A40 port and `kernel/0001-bluejay-port.patch`, and builds with `SWEET_BOARD=BLUEJAY`. Download the `xnu-kernel-bluejay` artifact (`kernel.development.sweet`).

Check it in QEMU first (no phone needed):

```sh
tools/qemu-smoke.sh path/to/kernel.development.sweet
```

With this kernel, `serial.txt` holds the whole kernel log (the shim passes `sweetuart=1` to the PL011 console).

## 4. Kernel boot without a root (read-only)

```sh
python3 tools/mkadt.py out/bluejay.adt          # D22AP_ADT=ipsw/d22ap.adt for /product
python3 tools/mkpayload.py kernel.development.sweet out/bluejay.adt out/payload.bin \
  --cmdline 'debug=0xc keepsyms=1 -enable_kprintf_spam -noprogress cpus=1 serial=3 rd=ufsroot0 wdt=-1'
python3 tools/mkbootimg.py shim/out/shim.o out/k --payload out/payload.bin --like factory/boot.img
fastboot boot out/k/boot.img
```

Without `ufsrw=1` the UFS driver is **read-only**. Expected: kernel console on screen, `SWEET UFS: "userdata" on LUN n`, then a panic because `userdata` holds Android's encrypted data instead of HFS+. That panic means storage works. Things to look for:

* `SWEET UFS: host not ready`: see step 1, UFS row.
* `command 0x.. failed: ocs 0x..`: try `ufsprdt=32` on the command line (FMP-format PRDT entries).
* No CPU or timer messages, or a hang right after `ATM subsystem is initialized`: timer or GIC (INTID 27, 24.576 MHz).
* Then try `cpus=8`.

## 5. The iOS root (erases Android's data)

Build the root image on a Mac with the A40 port's tools, which need no root on the Mac. The ownership fix-up happens on the first boot:

```sh
python3 iOS16-on-A40/tools/mkroot.py ipsw/OS.dmg ipsw/SystemOS.dmg ipsw/AppOS.dmg root.img 16
fastboot -S 512M flash userdata root.img        # wipes userdata; ~16 GiB
```

First boot = fixer boot: append `launchdsuffix=a40fix` to the command line in step 4 (and `ufsrw=1 rootrw=1`). It applies `/a40fix.manifest` and halts; give it 10 minutes. Then boot the normal command line from `iOS16-on-A40/README.md` (with `ufsrw=1 rootrw=1 launchd_unsecure_cache=1 ...`; leave out `sweetusb`, `sweetbattery` and `a40sec`, which do nothing here).

## 6. Getting back to Android

`fastboot -w` and `flash-all` from the factory image. Nothing outside `userdata` is ever written: the UFS driver refuses writes outside the partition it found, and `fastboot boot` never touches `boot`.

## Known gaps

* **Touch** is written (ST FTM5, `bsd/dev/bluejay_touch.c`) but depends on ABL leaving the touch supplies on; step 1 tells. If they are off, the shim now switches them on through the PMIC ACPM mailbox before handing control to XNU. Kernel/iOS input remains unverified.
* **No log reader**: there is no TWRP for the 6a, so the persistent log at the end of `userdata` can't be pulled the way sweet and the A40 do it. Use the screen and the UART.
* GPU, Wi-Fi, cellular, audio, cameras, USB, battery readout, power management: none.
