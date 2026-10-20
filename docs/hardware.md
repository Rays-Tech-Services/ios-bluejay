# Pixel 6a (bluejay, Google Tensor GS101): hardware notes

What the port relies on, with where each fact comes from and how sure it is.

* **Known**: from mainline Linux (`arch/arm64/boot/dts/exynos/google/gs101*.dts*`, `drivers/ufs/host/ufs-exynos.c`, `drivers/pinctrl/samsung/pinctrl-exynos-arm64.c`), the same on every GS101 phone.
* **VERIFY**: taken from the Pixel 6 (oriole) device tree or inferred, and not yet measured on a 6a. Each has a check in the shim's diagnostic boot ([bringup.md](bringup.md)), and each is read by the kernel from the ADT, so a wrong value is fixed in `tools/mkadt.py` without rebuilding XNU.

Nothing in this file has been tested on a real Pixel 6a yet.

## CPU

| | |
| --- | --- |
| Cores | 4 × Cortex-A55 (MPIDR 0x000–0x300), 2 × Cortex-A76 (0x400, 0x500), 2 × Cortex-X1 (0x600, 0x700). **Known** |
| ISA | ARMv8.2: LSE atomics, PAN, **16K granule on all three core types**, no pointer authentication. Same level as sweet's A55/A76, so sweet's kernel config fits (A40's ARMv8.0 changes are not needed) |
| MPIDR | Aff1 = core number, like sweet's DynamIQ cluster: `SweetCPU`'s topology check passes unchanged |
| SMP | PSCI 1.x through `smc`, `CPU_ON` with any 64-bit entry address (mainline boots all 8 cores this way). **Known** |
| Entry EL | Pixel ABL enters the kernel at **EL2** (pKVM/AVF need it). The shim drops to EL1 (HCR_EL2.RW, ICC_SRE_EL2, CNTHCTL_EL2, VPIDR/VMPIDR). Secondary cores also arrive at EL2; sweet's `sweet_secondary_entry` already handles that, and the bluejay patch adds VPIDR/VMPIDR. **VERIFY** (the shim shows CurrentEL) |

## Interrupts and timer

* GIC-600 (GICv3): GICD `0x10400000` (64 KiB), GICR `0x10440000` (8 × 128 KiB). Non-secure view, Group 1 IRQs: the same model as sweet. **Known**
* Architected timer PPIs are the standard ones: virtual timer = PPI 11 = **INTID 27** (sweet's Qualcomm numbering was 19). **Known**
* Timer frequency **24.576 MHz**; `gs101.dtsi` states it explicitly because the firmware does not reliably program CNTFRQ_EL0. The ADT carries it, and the shim falls back to it when CNTFRQ reads 0. **Known**

## Memory

* DRAM: 2 GiB at `0x80000000` + 4 GiB at `0x880000000` (6 GiB model). **Known for GS101; the 6a split is VERIFY**
* Firmware carve-outs (EL3, TEE, GSA, modem, splash, …) are fixed `reserved-memory` regions in the low bank. XNU gets **0x880000000 + 3.5 GiB**. The shim checks that range against the live FDT's `/memory` and every `reserved-memory` `reg` and refuses to boot on any overlap.

## Display

* Panel 1080 × 2400, a8r8g8b8 (BGRA in memory). ABL leaves the splash buffer scanning out at **`0xfac00000`** (mainline oriole: `simple-framebuffer` + `cont_splash_mem`). **VERIFY on bluejay**: the shim looks for a `simple-framebuffer` or a `*splash*`/`*framebuffer*` reservation in the live FDT and stops with a message if it disagrees.
* Mainline drives this buffer as a plain `simple-framebuffer`, so the DECON keeps refreshing it on its own: **no software trigger** (the A40 needed one). If writes do not show up, a trigger is the first suspect: add a `trigger` property to `sweet-fbcon` (the kernel already supports it).
* `0xfac00000` is not 32 MB aligned. The kernel's early console maps the framebuffer as one 32 MB L2 block, so the bluejay patch maps the containing block (`0xfa000000`) and adds the offset.

## Storage (UFS)

* Exynos UFS host: HCI `0x14700000`, vendor HCI `0x14701100` (`google,gs101-ufs`). Standard UFSHCI register layout plus Exynos quirks, all from `ufs-exynos.c`. **Known**
  * `UFSHCD_QUIRK_PRDT_BYTE_GRAN`: the UTRD response/PRDT offsets and lengths are in **bytes**, not dwords.
  * PRDT segments of at most **4 KiB** (Linux programs `HCI_TXPRDT/RXPRDT_ENTRY_SIZE` = 12).
  * `UFSHCI_QUIRK_BROKEN_REQ_LIST_CLR`: UTRLCLR clears with 1 bits (inverted).
  * DMA is limited to **36 bits** (Linux `exynos_ufs_set_dma_mask`), so the 64AS capability bit is not checked; XNU's RAM ends below 64 GiB anyway.
  * PRDT entry size 16 bytes without the Flash Memory Protector (mainline before FMP support worked this way). The ADT has `prdt-entry-size` in case ABL left FMP's 32-byte format on.
* sweet's driver takes over the host ABL leaves running. Whether ABL leaves the link up on a Pixel is **VERIFY** (the diagnostic boot reads HCE/HCS). If it does not, a full Exynos UFS + PHY bring-up is needed (not written).
* `userdata` is expected on LUN 0; the kernel tries LUNs 0–7 for the partition name (`ufsroot=`, default `userdata`). **VERIFY**

## Input

* Buttons are plain GPIOs (mainline oriole `gpio-keys`), active low:
  power `gpa10-1` (ALIVE `0x174d0000`, bank +0xe0), volume up `gpa8-1` and volume down `gpa7-3` (FAR_ALIVE `0x174e0000`, banks +0x40 / +0x20). DAT = bank + 4. **VERIFY on bluejay** (diagnostic boot shows them live)
* Touch: a Goodix controller on SPI on the 6a (sweet has a Goodix GT9896 over I²C, same Berlin protocol family). **Not ported yet**: needs the SPI (USI) transport, the power rails and the attention GPIO measured first.

## Debug output

* UART: USI `serial_0` at `0x10a00000`, Samsung (exynos4210-style) registers, 32-bit access only. It is on the USB-C SBU pins after `fastboot oem uart enable`; you need a USB-C breakout or Google's debug cable. If the port is clock-gated, touching it can hang the bus, so the shim only uses it when ABL's command line shows a serial console (`ttySAC`/`earlycon`) or the payload command line has `sweetuart=1`, and only then passes `sweetuart=1` to the kernel. **VERIFY**
* Without a UART: the framebuffer console (as on sweet and the A40) and the persistent log at the end of `userdata`.

## Watchdog

* `watchdog@10060000` and `watchdog@10070000` (Samsung `WTCON` at +0). If ABL left one running the phone would reset after a few seconds of XNU. The shim writes `WTCON = 0` to every watchdog node in the live FDT. **VERIFY** (the diagnostic boot shows WTCON before and after)

## Boot image

* `fastboot boot` of an Android boot image **v4** (header + kernel + generic ramdisk). ABL adds the flashed `vendor_boot` ramdisk *in front of* our ramdisk, so the XNU payload is embedded in the kernel `Image` itself (offset/size in the Image header, as the A40 loader does); the initrd is also searched as a fallback.
* `os_version` / `os_patch_level`: copy them from your stock `boot.img` (`--like`). **Do not invent a future patch level**: the Titan M2's KeyMint remembers the highest one it saw, and Android would then refuse to start with older keys.
