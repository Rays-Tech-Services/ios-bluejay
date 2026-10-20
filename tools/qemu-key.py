#!/usr/bin/env python3
"""Press a button of the emulated Pixel 6a in a running QEMU.

  qemu-key.py OUTDIR [power]

OUTDIR is the qemu-bluejay.py output directory (it holds mon.sock), e.g.
out/run or out/ios/run. "power" (the only key for now) sends QEMU's
system_powerdown, which pulses virt's PL061 GPIO pin 3 for ~100 ms; the
kernel's gpio-keys poller reports it as the power button (mkadt.py --qemu).
"""
import json, os, socket, sys

outdir = sys.argv[1] if len(sys.argv) > 1 else 'out/ios/run'
key = sys.argv[2] if len(sys.argv) > 2 else 'power'
if key != 'power':
    sys.exit('only "power" is wired up')
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(outdir, 'mon.sock'))
f = s.makefile('rw')
f.readline()
for cmd in ('qmp_capabilities', 'system_powerdown'):
    f.write(json.dumps({'execute': cmd}) + '\n')
    f.flush()
    print(cmd, f.readline().strip())
