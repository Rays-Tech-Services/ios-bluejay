#!/usr/bin/env python3
"""Print the kernel's persistent console log from a GPT disk image.

  disk-log.py DISK.img [OUT.txt]

The kernel (sweet patch 0047) copies its console ring every few seconds to
the last 4 MiB + 4 KiB of the "userdata" partition. This finds that
partition in the GPT (512-byte sectors, as tools/mkdisk.py makes them) and
decodes the ring with tools/sweet-log.py's format. Works while QEMU is
running: it shows the log up to the last flush, including what the console
mute (sweetfb_noconsole=1) kept off the serial port.
"""
import os, struct, subprocess, sys, tempfile

disk, out = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
with open(disk, 'rb') as f:
    f.seek(512)
    hdr = f.read(92)
    if hdr[:8] != b'EFI PART':
        sys.exit('no GPT')
    lba, n, esz = struct.unpack_from('<QII', hdr, 72)
    f.seek(lba * 512)
    ents = f.read(n * esz)
    for i in range(n):
        e = ents[i * esz:(i + 1) * esz]
        if e[56:56 + 16].decode('utf-16-le', 'ignore').rstrip('\0') == 'userdata':
            first, last = struct.unpack_from('<QQ', e, 32)
            break
    else:
        sys.exit('no userdata partition')
    nblk = (last - first + 1) * 512 // 4096
    f.seek(first * 512 + (nblk - 1025) * 4096)
    area = f.read(1025 * 4096)
with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as t:
    t.write(area)
cmd = [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sweet-log.py'), t.name]
sys.exit(subprocess.call(cmd + ([out] if out else [])))
