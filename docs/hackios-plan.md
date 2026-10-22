# What HackiOS does differently, and what to bring to the Pixel 6a

Source: [oosexgang/hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel) (APSL 2.0) and
[hackios-xnu-userland](https://github.com/oosexgang/hackios-xnu-userland) (MIT), read on 2026-10-22 from shallow
clones in `research/hackios/` (gitignored). HackiOS runs unmodified iOS 16.5 on a POCO X5 Pro 5G (Snapdragon 778G,
Adreno 642L) and in QEMU. Credit OOSEX GANG in commits that reuse their code.

## Why it works so well

It is not one trick. It is seven habits, and most of them are about iteration speed and safety, not hardware.

1. **Nothing is flashed.** `fastboot boot` starts a UEFI plus their loader. The loader uploads the iOS system image
   into RAM over its own USB link (2.5 GB, about 80 s). Each session runs for N seconds, then the kernel returns to
   the loader, which hands back the full log. `xl` also pulls screenshots, screen and sound recordings, and camera
   frames. A bad build costs one session, never a wiped partition.
2. **Scripted scenarios.** Their own `init` (process 1) runs job lists (`rc.jobs.*`, over 120 of them), for example
   unlock, a tour of every app, Safari, Wi-Fi join, sound, camera, persistence and Metal. Every feature has a
   repeatable test that runs without anyone touching the phone.
3. **QEMU parity.** Every subsystem has a virtio counterpart (input, block, net, sound). Almost all user-space work
   is done in QEMU, and the phone is only for hardware drivers.
4. **They replace Apple's closed drivers instead of imitating hardware.** MiniIOSurface, MiniIOMobileFramebuffer,
   MiniAppleKeyStore (no-passcode keybag, so the keychain works), a network stack, miniamfi/minimac, minicrypto
   (a corecrypto replacement), and their own APFS read/write. Each was worked out from the iOS user-space side,
   and anything not implemented is logged once.
5. **Injection, never modified iOS files.** The kernel injects libraries into backboardd, SpringBoard,
   mediaserverd and apps (`kern.exec_inject`). These add virtual HID services and a few ObjC swaps. The GL library
   is loaded **only if the GPU service exists**; otherwise CoreAnimation keeps its software renderer
   (`inject.c` `opengl_es()`, `my_renderer()`).
6. **Vendor firmware and Linux drivers do the heavy lifting.** The GPU runs Qualcomm's GMU/SQE/zap firmware, so
   the GMU firmware handles power. Wi-Fi is Linux's **unmodified ath11k** on a LinuxKPI layer (`wifi/lx`), and
   audio uses the ADSP firmware. All firmware is pulled from the phone's own Android.
7. **Step-by-step bring-up with a level per step.** `adreno=N` and `wifi_step=N` run steps 1 to N. Reads come
   before writes, each step logs what it will touch and what it found, and the sequence stops at the first
   surprise. Every feature is off unless a boot-arg asks for it, and a failure falls back to software.
   `docs/checks.md` lists eleven ways a check can lie. It is worth reading.

## Where the Pixel 6a stands

| Area | HackiOS (redwood) | Pixel 6a today | Bring over? |
|---|---|---|---|
| Boot/iteration | RAM image, session timer, `xl` | root on userdata; every root change means a wipe, a fixer boot and a slow first boot | **Yes, top priority** (Phase 1) |
| Display | MiniIOMobileFramebuffer | SweetFramebuffer (works, TE sync) | Compare only |
| IOSurface | MiniIOSurface | SweetSurface | Compare only |
| Keybag/keychain | MiniAppleKeyStore | SweetKeyStore (some `0xe00002c7` refusals in logs) | Diff their selector table against ours |
| GPU | Adreno via GMU firmware + freedreno | Mali-G78: shader cores do not power on | Our own work (Phase 0); copy their gating |
| GL in backboardd | loaded only when the GPU is up | fixed on 2026-10-22 (not yet built) | Done in `tools/sweetgl` |
| Touch, keys | Goodix, buttons | bluejay_touch, sweet_keys | Have |
| Battery, CPU clocks | redwood_battery, cpufreq | SweetBattery, shim ACPM DVFS | Have |
| Networking | Wi-Fi (ath11k) + stack | USB Ethernet only | Wi-Fi in Phase 3 |
| Sound | ADSP + CoreAudio plug-in + speaker guard | none (behind AoC) | Plug-in yes; hardware is hard |
| Haptics, torch | own drivers | none | Phase 3 |
| Camera | ISP driver | none | Not planned |
| SSH, Sileo, tweaks | Procursus bootstrap | none | **Yes** (Phase 2), big development win |
| Persistence | `var_store`, flash writes | root is read/write on UFS | Have |
| Metal | AIR → NIR frontend, 293/456 shaders | none | Long-term (Phase 4) |

## Phase 0: GPU shader cores (the blocker)

Facts from the 2026-10-22 phone boot: PD_G3D and the cores domain (`0x17462004`) read on. GPU_ID is `0x92020010`
and reset works. L2 and the tiler power up, but the shaders and the manual stack power-up time out
(stack trans `0x7f`, ready 0).

**Hypothesis (unverified): the shader clock domain is not running.** Google's GPU node
(`research/gs101/dts/gs101-gpu.dtsi`) has two ACPM DVFS clock domains: `gpu0_cmu_cal_id = ACPM_DVFS_G3DL2`
(L2/top) and `gpu1_cmu_cal_id = ACPM_DVFS_G3D` (shader cores), with operating points from 151 MHz up to
848 MHz. Our kernel never asks ACPM for either. L2 and tiler working while shaders hang is what you would see if
the G3DL2 clock runs and the G3D clock is stopped or too slow. The voltage rails (BUCK2S VDD_G3D, BUCK8S
VDD_G3D_L2) are in `SEC_OPMODE_SUSPEND` and follow the PMU's G3D enable in hardware, so they are probably on
already.

Steps (each needs Ray's OK for a boot that halts **before** iOS):

1. **Read-only.** Query ACPM DVFS `FREQ_GET` for the candidate G3D and G3DL2 domain IDs, and read the BUCK2S and
   BUCK8S control/voltage registers through ACPM PMIC reads (the same transport as the shim's `acpm_dvfs()` and
   `acpm_pmic()`). The domain numbers are not confirmed. The shim uses CPUCL0 = 2, and Google's usual ordering
   would make G3D 5 and G3DL2 6, but the query must show plausible kHz before anything is written.
2. **One write.** Request the lowest operating point (G3DL2 302000, G3D 151000 kHz), read it back, then rerun the
   level-2 power test. Success is `shader ok` with ready `0x779fff`.
3. Then level 3 (the `GPU WROTE IT` job), and only then a renderer boot.
4. Copy HackiOS's gating: register SweetGPU **only after** the self-test passes. Then a dead GPU looks like "no
   GPU" to user space from the start, as with RedwoodGPU.

## Phase 1: stop wiping userdata to test things

Every root change today costs a userdata wipe, a fixer boot and a slow first boot. HackiOS never flashes anything.

- **Short term:** push files over the USB network we already have. A small root-only receive daemon (or dropbear,
  see Phase 2) lets us replace `/usr/lib/sweetgl.dylib` and similar files without reflashing.
- **Kernel screenshot and log pull over USB.** We have the UDP log. Add a framebuffer dump so a boot can be
  checked without a photo from Ray.
- **Scripted scenarios** in the style of `rc.jobs` (unlock, open each app, Safari), driven by injected touch,
  with results in the log.
- **Longer term:** a RAM root (upload over USB, as `xl ramdisk` does). The Pixel bootloader limits
  `fastboot boot` size, so this needs our own USB transfer in the shim, which is a large job.

## Phase 2: hardware-independent HackiOS pieces

These only need reading their code and adapting it to our service names (MIT/APSL, with credit):

- `inject_sb.c` / `inject_tweaks.c`: SpringBoard fixes; diff them against what we already patch.
- MiniAppleKeyStore's selector table compared with SweetKeyStore (we refuse selector 10 in `securityd`).
- `audio_plugin.c` (CoreAudio output device) plus a `/dev` audio sink. Even with no speaker yet, it unblocks apps
  that wait on audio, and it can record to the log as their `rec_sound` does.
- Procursus + Sileo + SSH (`tools/mk-jb.sh`, `jb-preinstall.py`): a shell on the phone over USB Ethernet would
  have shortened every investigation so far.
- `docs/checks.md` habits: put each number with its exceptions in `docs/working-state.md`.

## Phase 3: Pixel hardware, using their methods

Each one follows their step-by-step pattern, off by default:

- **Wi-Fi:** BCM4389 on PCIe. Their LinuxKPI layer (`wifi/lx`) is chip-independent. Open questions are whether
  Linux's brcmfmac supports the 4389 (Google ships the out-of-tree bcmdhd), and the GS101 PCIe bring-up. Large.
- **Haptics:** a Cirrus haptic driver on I2C (part number to confirm from the 6a device tree). Moderate.
- **Torch:** the flash LED driver (to identify). Small to moderate.
- **Sound:** speakers and microphones sit behind the AoC coprocessor, so this needs AoC firmware loading. Large.
  The CoreAudio plug-in from Phase 2 comes first.
- **USB after a normal power-on:** the GS101 USB PHY (see `docs/codex-handoff.md` §6).

## Phase 4: Metal

Their AIR (Metal IR) → NIR frontend is in Mesa, not Adreno-specific. Once Panfrost runs, the same frontend could
feed Mesa's Valhall compiler. This is research-grade work for later.

## Order

Phase 0 step 1 (read-only, one boot that halts before iOS) → Phase 1 file push and screenshots → Phase 0 steps
2–4 → Phase 2 → Phase 3, one device at a time.
