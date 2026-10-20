#!/bin/bash
# End-to-end QEMU smoke test of the Pixel 6a boot chain.
#
#   tools/qemu-smoke.sh [KERNEL_SOURCE] [SECS]
#
# KERNEL_SOURCE: a kernel Mach-O (kernel.development.sweet from the
# xnu-bluejay workflow), or any SWEETXNU payload / Image / boot.img to take
# the kernel from (default: the A40 port's prebuilt boot image, so the chain
# can be tested before the bluejay kernel is built).
#
# Runs, in out/smoke/:
#   1. diag:   shim without payload, entered at EL2 -> diagnostic screen
#   2. initrd: shim + payload found in the initrd behind a fake vendor ramdisk
#   3. boot:   shim + payload in the Image + GPT/HFS+ test disk on virtio-blk,
#              must reach "load_init_program" (exec of /sbin/launchd; the test
#              root is empty, so the panic after that is expected)
# Needs qemu-system-aarch64 and macOS (hdiutil for the HFS+ test volume).
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=${1:-iOS16-on-A40/prebuilt/boot-ios16-a40.img}
SECS=${2:-240}
O=out/smoke
mkdir -p $O
XNU=${XNU_SRC:-build/xnu}
font=(); [ -f "$XNU/osfmk/console/iso_font.c" ] && font=(--xnu-src "$XNU")
CMD='debug=0xc keepsyms=1 -enable_kprintf_spam -noprogress cpus=1 serial=3 sweetusb=0 sweetbattery=0 rd=ufsroot0 wdt=-1'
fail=0
check() {	# name, file, pattern
	if grep -q -- "$3" "$2" 2>/dev/null; then echo "PASS $1"; else echo "FAIL $1 (no '$3' in $2)"; fail=1; fi
}

shim/build.sh >/dev/null
python3 tools/mkadt.py $O/qemu.adt --qemu 2>/dev/null
if [ "$(head -c 4 "$SRC" | xxd -p)" = "cffaedfe" ]; then
	python3 tools/mkpayload.py "$SRC" $O/qemu.adt $O/payload.bin --cmdline "$CMD" >/dev/null
else
	python3 tools/repayload.py "$SRC" $O/payload.bin --adt $O/qemu.adt --cmdline "$CMD" >/dev/null
fi
python3 tools/mkbootimg.py shim/out/shim.o $O/diag >/dev/null 2>&1
python3 tools/mkbootimg.py shim/out/shim.o $O/boot --payload $O/payload.bin >/dev/null 2>&1

if [ ! -f $O/disk.img ]; then
	rm -f $O/root.cdr
	hdiutil create -quiet -size 64m -fs 'Case-sensitive Journaled HFS+' -volname SmokeRoot -layout NONE -type UDTO -o $O/root
	python3 tools/mkdisk.py $O/root.cdr $O/disk.img >/dev/null
fi

echo "== 1. diagnostic screen (EL2 entry)"
python3 tools/qemu-bluejay.py $O/diag/Image $O/q-diag --secs 6 >/dev/null 2>&1 || true
check "EL2 -> EL1" $O/q-diag/serial.txt "entry EL 0x00000002  now EL 0x00000001"
check "diagnostic" $O/q-diag/serial.txt "DIAGNOSTIC BOOT"
check "memory map" $O/q-diag/serial.txt "memory 0x0000000880000000"
check "screen" $O/q-diag/fb.txt "bluejay shim"

echo "== 2. payload in the initrd"
python3 tools/qemu-bluejay.py $O/boot/Image $O/q-initrd --initrd-payload --secs 25 >/dev/null 2>&1 || true
check "found in initrd" $O/q-initrd/serial.txt "in the initrd"
check "jump" $O/q-initrd/serial.txt "jumping to XNU"

echo "== 3. boot to launchd (${SECS}s)"
python3 tools/qemu-bluejay.py $O/boot/Image $O/q-boot --disk $O/disk.img --snapshot --secs "$SECS" "${font[@]}" >/dev/null 2>&1 || true
check "jump" $O/q-boot/serial.txt "jumping to XNU"
both=$O/q-boot/all.txt
cat $O/q-boot/serial.txt $O/q-boot/fb.txt > $both
# The framebuffer console wraps; only a kernel with the sweet-uart console
# (the bluejay kernel, given sweetuart=1 by the shim) keeps the whole log.
if grep -q "SWEET" $O/q-boot/serial.txt; then
	check "framebuffer driver" $O/q-boot/serial.txt "AppleCLCD 1080x2400"
else
	echo "SKIP kernel serial checks (this kernel has no sweet-uart console)"
fi
check "virtio disk" $both "virtio-blk"
check "userdata found" $both '"userdata" on LUN'
check "HFS+ root mounted" $both "hfs: mounted SmokeRoot"
check "launchd exec" $both "load_init_program"
[ ${#font[@]} -eq 0 ] && echo "note: no xnu source at $XNU: kernel lines only checked in serial.txt"
exit $fail
