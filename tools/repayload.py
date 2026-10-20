#!/usr/bin/env python3
"""Give an existing SWEETXNU payload a new ADT and/or command line.

  repayload.py SOURCE OUT.bin [--adt NEW.adt] [--cmdline "..."]

SOURCE is a payload (tools/mkpayload.py output), an arm64 Image with an
embedded payload (fields 0x20/0x28), or an Android boot.img whose kernel is
such an Image (e.g. iOS16-on-A40/prebuilt/boot-ios16-a40.img). The kernel
segments are kept as they are; phys_base/mem_size follow the new ADT's
/chosen dram-base/dram-size, like mkpayload.py.

Useful to run an already built kernel under QEMU with the bluejay QEMU tree
before the bluejay kernel itself is built (docs/qemu.md).
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkpayload  # noqa: E402


def extract(blob):
    if blob[:8] == b'SWEETXNU':
        return blob
    if blob[:8] == b'ANDROID!':
        ksize, = struct.unpack_from('<I', blob, 8)
        ver, = struct.unpack_from('<I', blob, 40)
        page = 4096 if ver >= 3 else struct.unpack_from('<I', blob, 36)[0]
        blob = blob[page:page + ksize]
    if blob[0x38:0x3c] == b'ARM\x64':
        off, size = struct.unpack_from('<QQ', blob, 0x20)
        if size and blob[off:off + 8] == b'SWEETXNU':
            return blob[off:off + size]
    sys.exit('no SWEETXNU payload found')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source')
    ap.add_argument('out')
    ap.add_argument('--adt')
    ap.add_argument('--cmdline')
    a = ap.parse_args()
    p = bytearray(extract(open(a.source, 'rb').read()))
    nsegs, = struct.unpack_from('<I', p, 12)
    adt_off, adt_size = struct.unpack_from('<QQ', p, 0x20)
    old_cmd = p[mkpayload.CMDLINE:mkpayload.SEG_TABLE].split(b'\0', 1)[0].decode()
    print(f'source: {nsegs} segments, ADT {adt_size} bytes, cmdline: {old_cmd}')
    if a.adt:
        adt_blob = open(a.adt, 'rb').read()
        if any(struct.unpack_from('<4Q', p, mkpayload.SEG_TABLE + 32 * i)[2] > adt_off for i in range(nsegs)):
            sys.exit('a segment lies behind the ADT; cannot replace it in place')
        p = p[:adt_off] + adt_blob
        p += b'\0' * (-len(p) & 0xf)
        base, size = mkpayload.adt_dram(adt_blob)
        struct.pack_into('<QQ', p, 0x20, adt_off, len(adt_blob))
        struct.pack_into('<QQ', p, 0x38, base, size)
        print(f'new ADT {len(adt_blob)} bytes, RAM {base:#x} + {size:#x}')
    if a.cmdline is not None:
        cl = a.cmdline.encode()[:mkpayload.BOOT_LINE_LENGTH - 1]
        p[mkpayload.CMDLINE:mkpayload.SEG_TABLE] = cl.ljust(mkpayload.BOOT_LINE_LENGTH, b'\0')
        print(f'cmdline: {a.cmdline}')
    open(a.out, 'wb').write(p)


if __name__ == '__main__':
    main()
