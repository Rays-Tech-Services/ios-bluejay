#!/bin/bash
# Launch the Pixel 6a build in QEMU and watch it boot.
#
#   ./run-qemu.sh                     # kernel from out/ci (the xnu-bluejay CI artifact)
#   ./run-qemu.sh --kernel PATH       # another kernel.development.sweet (or payload/boot.img)
#   ./run-qemu.sh --disk root.img     # a GPT disk with an iOS root in "userdata" (tools/mkdisk.py)
#   ./run-qemu.sh --diag              # only the shim's diagnostic screen
#
# The kernel log scrolls in this terminal. The phone screen (1080x2400) is
# saved to out/run/fb.png every 3 s; out/run/screen.html shows it, refreshing
# itself, and opens in your browser. Ctrl-C stops QEMU (the last screen and
# the decoded text stay in out/run/).
set -euo pipefail
cd "$(dirname "$0")"

KERNEL=out/ci/kernel.development.sweet
DISK=
DIAG=0
while [ $# -gt 0 ]; do
	case "$1" in
	--kernel) KERNEL=$2; shift 2 ;;
	--disk) DISK=$2; shift 2 ;;
	--diag) DIAG=1; shift ;;
	*) echo "unknown option $1 (see the top of $0)"; exit 1 ;;
	esac
done

command -v qemu-system-aarch64 >/dev/null || { echo "QEMU missing: brew install qemu"; exit 1; }
O=out/run
mkdir -p $O
shim/build.sh >/dev/null

if [ $DIAG = 1 ]; then
	python3 tools/mkbootimg.py shim/out/shim.o $O/img >/dev/null 2>&1
else
	[ -f "$KERNEL" ] || { echo "No kernel at $KERNEL: download the xnu-kernel-bluejay artifact from the"
		echo "xnu-bluejay workflow on GitHub and unzip it into out/ci/, or pass --kernel."; exit 1; }
	python3 tools/mkadt.py $O/qemu.adt --qemu 2>/dev/null
	CMD='debug=0xc keepsyms=1 -enable_kprintf_spam -noprogress cpus=1 serial=3 rd=ufsroot0 wdt=-1'
	if [ "$(head -c 4 "$KERNEL" | xxd -p)" = "cffaedfe" ]; then
		python3 tools/mkpayload.py "$KERNEL" $O/qemu.adt $O/payload.bin --cmdline "$CMD" >/dev/null
	else
		python3 tools/repayload.py "$KERNEL" $O/payload.bin --adt $O/qemu.adt --cmdline "$CMD" >/dev/null
	fi
	python3 tools/mkbootimg.py shim/out/shim.o $O/img --payload $O/payload.bin >/dev/null 2>&1
	if [ -z "$DISK" ]; then
		# empty HFS+ root: boots up to launchd, which is not there
		DISK=$O/empty-disk.img
		if [ ! -f $DISK ]; then
			rm -f $O/empty-root.cdr
			hdiutil create -quiet -size 64m -fs 'Case-sensitive Journaled HFS+' -volname EmptyRoot \
				-layout NONE -type UDTO -o $O/empty-root
			python3 tools/mkdisk.py $O/empty-root.cdr $DISK >/dev/null
		fi
		echo "No --disk given: using an empty root, so the boot ends at 'exec of /sbin/launchd failed'."
	fi
fi

cat > $O/screen.html <<'EOF'
<!doctype html><meta charset="utf-8"><title>Pixel 6a (QEMU)</title>
<body style="margin:0;background:#111;display:flex;justify-content:center">
<img id="s" style="height:100vh" src="fb.png">
<script>setInterval(()=>{document.getElementById('s').src='fb.png?'+Date.now()},2000)</script>
EOF
[ -z "${NOOPEN:-}" ] && ( sleep 4; open $O/screen.html ) &

xnu=(); [ -f build/xnu/osfmk/console/iso_font.c ] && xnu=(--xnu-src build/xnu)
disk=(); [ -n "$DISK" ] && disk=(--disk "$DISK" --snapshot)
echo "Booting... (Ctrl-C to stop)"
python3 tools/qemu-bluejay.py $O/img/Image $O --live --watch 3 --secs 0 "${disk[@]}" "${xnu[@]}"
