#!/bin/bash
# Boot real iOS 16 (iPhone X userland) on the Pixel 6a build in QEMU.
#
#   tools/qemu-ios.sh [ROOT.img] [KERNEL]
#
# ROOT.img: the HFS+ root from iOS16-on-A40/tools/mkroot.py (default out/root.img)
# KERNEL:   kernel.development.sweet from the xnu-bluejay workflow (default out/ci/)
#
# First run: wraps the root in a GPT disk (out/ios/disk.img, sparse, writable)
# and does the one-time fixer boot (launchd.a40fix applies /a40fix.manifest:
# owners and modes the Mac could not set) until it prints
# "A40 FIX: halting". Then, and on every later run, the normal iOS boot:
# kernel log in this terminal, the screen in out/ios/screen.html, Ctrl-C to stop.
# Under QEMU's software CPU expect tens of minutes to SpringBoard.
# Delete out/ios/disk.img to start over from the pristine ROOT.img.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=${1:-out/root.img}
KERNEL=${2:-out/ci/kernel.development.sweet}
O=out/ios
mkdir -p $O
[ -f "$ROOT" ] || { echo "no root image at $ROOT"; exit 1; }
[ -f "$KERNEL" ] || { echo "no kernel at $KERNEL"; exit 1; }

BASE='debug=0xc keepsyms=1 -enable_kprintf_spam -noprogress cpus=1 serial=3 rd=ufsroot0 ufsrw=1 rootrw=1 wdt=-1'
FIX="$BASE launchdsuffix=a40fix"
IOS="$BASE unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 sweetfb_hz=30 sweetfb_noconsole=1"

shim/build.sh >/dev/null
python3 tools/mkadt.py $O/qemu.adt --qemu 2>/dev/null
image() {	# name cmdline
	python3 tools/mkpayload.py "$KERNEL" $O/qemu.adt $O/$1.bin --cmdline "$2" >/dev/null
	python3 tools/mkbootimg.py shim/out/shim.o $O/$1 --payload $O/$1.bin >/dev/null 2>&1
}
xnu=(); [ -f build/xnu/osfmk/console/iso_font.c ] && xnu=(--xnu-src build/xnu)

if [ ! -f $O/disk.img ] || [ ! -f $O/fixed ]; then
	if [ ! -f $O/disk.img ]; then
		echo "Making the GPT disk from $ROOT..."
		python3 tools/mkdisk.py "$ROOT" $O/disk.img
	fi
	echo "Fixer boot (one time): applying owners and modes, then halting..."
	image fix "$FIX"
	python3 tools/qemu-bluejay.py $O/fix/Image $O/run-fix --disk $O/disk.img --secs 7200 \
		--until 'A40 FIX: halting|panic\(cpu' "${xnu[@]}" >/dev/null 2>&1 || true
	if grep -q 'A40 FIX: halting' $O/run-fix/serial.txt; then
		touch $O/fixed
		grep 'A40 FIX' $O/run-fix/serial.txt | tail -3
	else
		echo "Fixer boot did not finish; see $O/run-fix/serial.txt and fb.png"
		tail -20 $O/run-fix/serial.txt
		exit 1
	fi
fi

image ios "$IOS"
cat > $O/screen.html <<'EOF'
<!doctype html><meta charset="utf-8"><title>iOS on Pixel 6a (QEMU)</title>
<body style="margin:0;background:#111;display:flex;justify-content:center">
<img id="s" style="height:100vh" src="run/fb.png">
<script>setInterval(()=>{document.getElementById('s').src='run/fb.png?'+Date.now()},2000)</script>
EOF
[ -z "${NOOPEN:-}" ] && ( sleep 6; open $O/screen.html ) &
echo "Booting iOS... (Ctrl-C to stop; the screen is in $O/screen.html)"
python3 tools/qemu-bluejay.py $O/ios/Image $O/run --disk $O/disk.img --live --watch 5 \
	--secs "${SECS:-0}" ${UNTIL:+--until "$UNTIL"} "${xnu[@]}"
