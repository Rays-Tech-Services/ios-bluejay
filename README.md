# iOS 16 for the Pixel 6a

A port of [ios-sweet](https://github.com/dipa-studio/ios-sweet) (iOS 16 on the Redmi Note 10 Pro) and [iOS16-on-A40](https://github.com/tlfares/iOS16-on-A40) (the same on the Galaxy A40) to the **Google Pixel 6a** (`bluejay`, Google Tensor / GS101), plus a **QEMU setup** that runs the Pixel boot chain on a Mac or Linux PC.

> **Status (2026-10-20): not yet run on a Pixel 6a.** The boot shim, device tree, payload and boot image tooling run under QEMU, and an existing XNU build boots through them to `exec /sbin/launchd`. The bluejay kernel patch applies cleanly but has **not been compiled** yet (it needs the macOS CI build below). The hardware facts it relies on come from mainline Linux and the Pixel 6 device tree; the ones not measured on a 6a are marked **VERIFY** in [docs/hardware.md](docs/hardware.md), and the shim's diagnostic boot checks each of them.

## Why the Pixel 6a is a good target

* All eight Tensor cores (A55, A76, X1) are ARMv8.2 with the **16K page granule** iOS needs. The A40 could only use 2 of its 8 cores.
* GICv3, PSCI, MPIDR with Aff1 = core: the same model as sweet, so sweet's interrupt and SMP code fits as is.
* Exynos-derived peripherals (UFS, UART, pin controller), like the A40's Exynos.
* An unlocked Pixel can `fastboot boot` an image without flashing anything.

## How it boots

```
Pixel ABL (unlocked) -- fastboot boot boot.img (header v4), enters at EL2
  -> shim (shim/shim.c): EL2 -> EL1, checks the live device tree, stops watchdogs
     -> XNU payload appended to the shim's Image: kernel + Apple Device Tree + boot-args
        -> XNU (sweet + A40 + bluejay patches) -> Exynos UFS -> HFS+ root in userdata
           -> launchd -> SpringBoard (iPhone X userland, CPU rendering)
```

## Repository layout

| Path | What |
| --- | --- |
| `shim/` | Boot shim (C + inline asm), built with Xcode's clang only, no linker |
| `kernel/0001-bluejay-port.patch` | XNU patch on top of ios-sweet 0001–0081 + the A40 port |
| `.github/workflows/xnu-bluejay.yml` | Builds the kernel on GitHub's macOS runner |
| `tools/mkadt.py` | Apple Device Tree for bluejay (`--qemu` for the emulator) |
| `tools/mkpayload.py` | Kernel + ADT + boot-args → SWEETXNU payload (from ios-sweet) |
| `tools/mkbootimg.py` | Shim + payload → `Image` and Android `boot.img` v4 |
| `tools/qemu-bluejay.py` | Runs an `Image` in QEMU the way Pixel ABL would |
| `tools/qemu-smoke.sh` | End-to-end QEMU test (diagnostics, initrd fallback, boot to launchd) |
| `tools/repayload.py` | New ADT/command line for an existing payload |
| `tools/mkdisk.py`, `adt.py`, `sweet-log.py`, `remotezip.py` | Helpers from the A40 port / ios-sweet |
| `docs/hardware.md` | GS101/bluejay facts, sources, confidence |
| `docs/bringup.md` | Step-by-step plan for the first boots on a real phone |

`ios-sweet/` and `iOS16-on-A40/` (the upstream projects, their own git repos) and `build/` (the patched XNU tree) are not part of this repository.

## QEMU

```sh
brew install qemu            # or: apt install qemu-system-arm
tools/qemu-smoke.sh          # ~5 min; uses the A40 port's prebuilt kernel until the bluejay one is built
tools/qemu-smoke.sh kernel.development.sweet   # with the bluejay kernel from CI
```

`tools/qemu-bluejay.py` boots the **same `Image` that goes into the phone's boot.img**. It acts as Pixel ABL: QEMU `virt` with GICv3 and EL2 enabled, the Image in the low DRAM bank, and a device tree shaped like the Pixel's (DRAM at `0x80000000` and `0x880000000`, firmware `reserved-memory`, the splash framebuffer at `0xfac00000`, PSCI, PL011 standing in for the Samsung UART). The payload has to be built with `mkadt.py --qemu`, which describes QEMU's GIC, PL011 and a virtio disk instead of the Pixel's UFS.

```sh
python3 tools/mkadt.py out/qemu.adt --qemu
python3 tools/mkpayload.py kernel.development.sweet out/qemu.adt out/payload.bin \
  --cmdline 'debug=0xc keepsyms=1 -enable_kprintf_spam cpus=1 serial=3 rd=ufsroot0 wdt=-1'
python3 tools/mkbootimg.py shim/out/shim.o out/q --payload out/payload.bin
python3 tools/qemu-bluejay.py out/q/Image out/run --disk disk.img --snapshot --secs 300 --watch 10
```

Output in `out/run/`: `serial.txt` (shim log, and the kernel log with the bluejay kernel), `fb.png` (the 1080×2400 screen, refreshed every `--watch` seconds), `fb.txt` (the screen as text; add `--xnu-src build/xnu` to read the kernel's font), `regs.txt`, `qemu.log`, `result.json`. Other options: `--el1` (enter at EL1), `--initrd-payload` (payload behind a fake vendor ramdisk), `--bootargs '... shimdiag'` (diagnostic screen), `--gdb`.

A disk with a real iOS root: build `root.img` as in `docs/bringup.md` step 5, then `python3 tools/mkdisk.py root.img disk.img`. A full boot to the lock screen took about 40 minutes under TCG on the A40 port.

What QEMU cannot test: the Pixel's UFS controller, buttons, Samsung UART, watchdog and real memory map. Those are what the diagnostic boot (`docs/bringup.md` step 1) checks on the phone.

## Building for the phone

1. **Kernel**: run the `xnu-bluejay` workflow on GitHub (the patch series and versions are pinned in it), or locally with [darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build): apply sweet 0001–0081, `iOS16-on-A40/kernel/0001-a40-port.patch`, then `kernel/0001-bluejay-port.patch`, and build with `MACHINE_CONFIG=SWEET SWEET_BOARD=BLUEJAY KERNEL_CONFIG=DEVELOPMENT`. Without `SWEET_BOARD=BLUEJAY` you get the A40 kernel.
2. **Shim and boot image**: `shim/build.sh`, then `tools/mkadt.py`, `tools/mkpayload.py`, and `tools/mkbootimg.py ... --like factory/boot.img`. Copy `os_version`/patch level from your factory image, and **don't invent a newer patch level**: the Titan M2 remembers the highest one it has seen.
3. **Root image and first boots**: [docs/bringup.md](docs/bringup.md).

## Kernel patch summary (`SWEET_BOARD=BLUEJAY`)

| Area | Change |
| --- | --- |
| Build | `-march=armv8.2-a+crypto`, PAN back on (the A40 build stays ARMv8.0) |
| Early console | Framebuffer `0xfac00000` mapped through its 32 MB block plus offset; no DECON trigger |
| SMP | Secondary cores arriving at EL2 get VPIDR/VMPIDR, so EL1 reads the real MPIDR |
| Console | `sweet-uart`: polled TX on PL011 (QEMU) or the Samsung UART (phone), only with `sweetuart=1` |
| UFS | Exynos mode from the ADT: byte-granular UTRD, 4 KiB PRDT segments, 16/32-byte PRDT entries (`ufsprdt=`), inverted UTRLCLR, 36-bit DMA; LUN from the ADT / `ufslun=`, else LUNs 0–7 are searched |
| Buttons | ADT-driven `gpio-keys` poller (DAT register, bit, key) |
| Safety | A `/bluejay` ADT node keeps sweet's Qualcomm SPMI, GENI, RPMh, USB and fuel-gauge code off the bus |

## Not done

Touch (Goodix over SPI), full UFS initialisation if ABL turns UFS off, reading the persistent log without a custom recovery, and everything ios-sweet lists as missing (GPU, radios, audio, cameras, power management). See [docs/bringup.md](docs/bringup.md#known-gaps).

## Credits

Built on [ios-sweet](https://github.com/dipa-studio/ios-sweet) and [iOS16-on-A40](https://github.com/tlfares/iOS16-on-A40) (kernel patches, tools, font, payload format), Apple's open-source XNU, [darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build), and the mainline Linux GS101 support (device tree, UFS, pin controller) for the hardware facts. iOS is Apple's software and nothing from Apple's IPSW is in this repository.
