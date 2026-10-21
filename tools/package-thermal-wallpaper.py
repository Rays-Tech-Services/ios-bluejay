#!/usr/bin/env python3
"""Package a rebuilt kernel with the thermal and PosterBoard opt-in fixes.

Requires a phone boot image as the source of the ADT and existing XNU flags,
and stock boot metadata via --like. Never boots or flashes the phone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import mkpayload
import repayload


def command_line(old):
    flags = {'sweetbattery': '2', 'sweetthermal': '1', 'sweetfix_posterboard': '1'}
    words = old.split()
    if any(w.startswith('rd=md') for w in words):
        raise ValueError('use a phone UFS boot image, not a ramdisk image')
    words = [w for w in words if w.split('=', 1)[0] not in flags]
    words.extend(f'{key}={value}' for key, value in flags.items())
    line = ' '.join(words)
    if len(line.encode()) >= mkpayload.BOOT_LINE_LENGTH:
        raise ValueError('boot arguments exceed the payload command-line limit')
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kernel', type=Path, help='kernel rebuilt with the updated tracked patch')
    ap.add_argument('source', type=Path, help='previous phone boot.img')
    ap.add_argument('outdir', type=Path, help='new output directory (must not exist)')
    ap.add_argument('--shim', required=True, type=Path, help='compiled shim.o')
    ap.add_argument('--like', required=True, type=Path, help='stock boot.img')
    args = ap.parse_args()
    payload = repayload.extract(args.source.read_bytes())
    off, size = struct.unpack_from('<QQ', payload, 0x20)
    if off + size > len(payload):
        raise ValueError('source ADT extends beyond payload')
    line = command_line(payload[mkpayload.CMDLINE:mkpayload.SEG_TABLE].split(b'\0', 1)[0].decode())
    # Read all inputs before creating any artifacts.
    inputs = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (args.kernel, args.source, args.shim, args.like)}
    args.outdir.mkdir(parents=True, exist_ok=False)
    adt = args.outdir / 'bluejay.adt'
    adt.write_bytes(payload[off:off + size])
    target = args.outdir / 'payload.bin'
    tools = Path(__file__).resolve().parent
    subprocess.run([sys.executable, str(tools / 'mkpayload.py'), str(args.kernel),
                    str(adt), str(target), '--cmdline', line], check=True)
    subprocess.run([sys.executable, str(tools / 'mkbootimg.py'), str(args.shim),
                    str(args.outdir), '--payload', str(target), '--like', str(args.like)], check=True)
    boot = args.outdir / 'boot.img'
    check = repayload.extract(boot.read_bytes())
    assert check[mkpayload.CMDLINE:mkpayload.SEG_TABLE].split(b'\0', 1)[0].decode() == line
    (args.outdir / 'manifest.json').write_text(json.dumps({
        'inputs_sha256': inputs, 'cmdline': line,
        'boot_sha256': hashlib.sha256(boot.read_bytes()).hexdigest(),
        'hardware_verified': False,
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
