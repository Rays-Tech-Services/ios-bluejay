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
interface exists. `--configure-link` rechecks its exact host MAC, then uses networksetup
to assign 192.168.7.1/24 to that interface's unique network service and ping the phone. `--listen` receives
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

## Real observer result (IMG_5930)

All identity/node checks passed. MAX77759 DevName 0x6200; RepSOC 0x627f
(98.496%), VCell 0xde6a (4448 mV), temperature 0x1600 (22 C). Charger
INT_OK 0xd9, DETAILS_00 0x60, DETAILS_01 0xb1, DETAILS_02 0x20 indicate
valid USB input and fast charging. Cable transition and iOS reporting remain
to verify. DWC3 GSNPSID is 0x33313130, VER_NUMBER 0x3138302a (DWC3.1).
The original kernel guard incorrectly compared the ID to a version-format
value; corrected to recognize the DWC3.1 ID family 0x3331xxxx, matching Linux.

## First iOS USB test

CI 39533700976 succeeded (kernel SHA256
cd93feacaa32f774814984f62974ae3a9da8a55c4159f798c070916dbb22973d).
Temporary eight-core boot of out/phone-ios-usb-battery-v2/boot.img succeeded.
Mac recognized Sweet iOS USB Ethernet and AppleUserECM / AppleUserECMData
matched; en5 appeared. Permanent MAC 02:00:5e:77:ee:01 was correct in
networksetup; ifconfig temporarily showed 02:00:00:00:00:00, so the helper
now identifies the interface by networksetup's permanent hardware address.
The new Sweet iOS network service was changed from DHCP to manual
192.168.7.1/24, no router. USB disconnected before packet tests completed.
Ping did not succeed; no kernel logs were captured. Awaiting whether user
unplugged or phone/driver disconnected. No NAT/forwarding/DNS changes made.

## Confirmed follow-up / active internet work

User confirmed iOS displays 99% and changes charging/unplugged status when
the cable is removed/reinserted. Battery UI goal achieved on the temporary
image; working installed image still has its previous static battery service.
The USB disappearance was user unplugging. Reconnect did not enumerate again.
Fresh temporary boot with cable left attached: Mac en5 reappeared, static
192.168.7.1 applied, 3/3 pings to 192.168.7.2 succeeded (2.4–5.1 ms), and
UDP kernel logging works (`out/usb-battery-live.log`, local/private).
User reports no Ethernet entry in iOS Settings. Existing cached Apple job
com.apple.usb.networking.addNetworkInterface waits for IONetworkInterface
with supported controller metadata; the old driver only published a BSD ifnet.

Next kernel experiment adds a minimal IOKit discovery adapter with self-named
en0, IFT_ETHER type, MAC and a supported IOUserNetworkEthernet controller
identity. This is discovery metadata, not a complete IONetworkingFamily or
DriverKit implementation; no user-client ABI. Apple's SCNetworkInterface.c
reads these properties; whether the iOS job successfully creates the DHCP
service still requires hardware verification. Pixel BSD interface becomes en0;
other boards retain ue0. No default route or DNS is hardcoded.
Also track DWC3 transfer resource IDs, end data transfers on reset/disconnect,
and rearm EP0 if interrupted mid control transfer, following upstream DWC3.
This is an attempted reconnect fix, not hardware verified yet.
