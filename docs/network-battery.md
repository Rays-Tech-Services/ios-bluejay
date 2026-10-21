# Pixel USB networking and battery bring-up

Status: experimental source prepared; not yet verified on the phone. Preserve
`backup/known-working-ios-8core-2026-10-20/boot.img` and installed boot_a.
Use temporary `fastboot boot` images for testing. Never reflash userdata.

## First hardware observation

`out/phone-power-usb/boot.img`, command line `shimpowerusb`, is a payload-free
observer. It reads MAX77759 fuel gauge and charger status via software I2C12,
then reads the DWC3 state left by fastboot. No storage writes, gauge configuration,
charger configuration, or USB controller writes. GPIOs are temporarily used as
open-drain and restored after each transaction; clock stretching has a timeout.
It checks the real FDT for Google's vendor identity `google,GS101 BLUEJAY`
(or the canonical identity) plus USB at 0x11110000 and UFS at 0x14700000
before hardware access. The first phone test was incorrectly skipped because
the initial guard only accepted the canonical identity; this is corrected. QEMU verified
that other hardware is skipped. Photograph the initial screen, then unplug/replug
the cable and compare SOC / USB status lines. This diagnostic does not boot iOS.

## Kernel test options

- `sweetbattery=2`: Pixel MAX77759 reported SOC, voltage, temperature and charger
  status, sampled every five seconds. This is the gauge SOC, not a voltage curve;
  Android's additional SOC smoothing is not implemented. Current and cycle count
  remain placeholders, explicitly current-valid=false. Nominal 4306 mAh is from
  Google's bluejay tree. Failed or implausible samples retain the preceding source;
  initial fallback is marked invalid. No charging limits or safety controls changed.
- `sweetusb=2`: opt-in Pixel DWC3 CDC-ECM gadget using inherited PHY/clocks.
  Unknown DWC3 IDs are rejected. QEMU is excluded by the ADT qemu property.
  Qualcomm register accesses and QSCRATCH overrides are skipped on Pixel.
  USB2 high-speed descriptors, local host MAC 02:00:5e:77:ee:01, phone MAC ...:02.
  The existing interface is ue0, 192.168.7.2/24, host 192.168.7.1.

Battery I2C uses GPP23 pins 4/5; touch SPI uses pins 0..3 on the same register
bank. A shared mutex protects bank RMW operations and battery mux restoration.
Hardware tests must confirm touch remains responsive during polling.

## Mac local-link helper

`python3 tools/pixel-usb-macos.py` only reports whether the matching CDC-ECM
interface exists. `--configure-link` rechecks its exact host MAC, then uses sudo
to assign 192.168.7.1/24 to that interface and ping the phone. `--listen` receives
UDP kernel logs after the local link is configured. This does not enable NAT,
forwarding or DNS and does not modify unrelated network interfaces.
Interface-selection tests pass; current Mac inspection found no Pixel ECM
interface yet, as expected before booting the experimental USB driver.

## Network milestones still required

1. Read inherited controller ID and state on the phone.
2. Enumerate CDC-ECM on the Mac and confirm bidirectional packets / ping.
3. Configure Mac forwarding/sharing plus phone default route and DNS.
4. Verify an iOS app actually reaches the internet, and cable reconnect works.

USB enumeration or a ping alone does not establish internet access. Existing
upstream networking only assigns a local IPv4 address; it does not configure a
phone default route/DNS or Mac internet sharing. Do not enable an unrelated Mac
network interface or change global PF rules while guessing the USB interface.

## Source evidence

- Google gs101-usi.dtsi: I2C12 0x10d50000; GPIO GPP23 pins 4/5.
- Linux pinctrl-exynos-arm64.c: GPP23 at PERIC1 0x10c40000 + 0x60.
- Google gs101-raviole-battery.dtsi: fuel gauge 0x36, charger 0x69.
- Google gs101-bluejay-battery.dtsi: nominal capacity 4306 mAh.
- [Google MAX77759 register definitions](https://android.googlesource.com/kernel/google-modules/bms/+/7a41aa0fb7f2fd9fa5f3642ddca803481d7948d8/max77759_v0p65_121919_regs.h):
  RepSOC 0x06, temperature 0x08, VCell 0x09, DevName 0x21;
  charger INT_OK 0xb4, DETAILS_01 0xb6, DETAILS_02 0xb7.
- [Google battery unit conversions](https://android.googlesource.com/kernel/google-modules/bms/+/7a41aa0fb7f2fd9fa5f3642ddca803481d7948d8/maxfg_common.h):
  SOC 1/256 percent, VCell 78.125 uV, signed temperature 1/256 C.
- [Google charger state definitions](https://android.googlesource.com/kernel/google-modules/bms/+/7a41aa0fb7f2fd9fa5f3642ddca803481d7948d8/max77759_charger.c):
  states 0..3 charging, 4 done; CHGIN_OK bit6, input path CHGIN_STS bit5.
- Google gs101.dtsi: DWC3 at 0x11110000, USB PHY and HSI0 domain.

`tools/battery-data-test.cpp` checks conversions, signed temperature, charger
fault states, missing input path and rejected implausible samples. The decoder
in `kernel/include/BluejayBatteryData.h` must match the header added by the port
patch. Full kernel CI and hardware measurements remain necessary.
