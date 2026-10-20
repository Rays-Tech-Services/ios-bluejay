#!/usr/bin/env python3
"""Pack the bluejay shim (+ XNU payload) into an arm64 Image and a boot.img v4.

  mkbootimg.py SHIM.o OUT_DIR [--payload PAYLOAD.bin] [--like STOCK_boot.img]
                              [--os-version 14.0.0 --patch-level 2024-07]

Writes OUT_DIR/Image (what QEMU and ABL run) and OUT_DIR/boot.img (for
`fastboot boot`). The payload is appended to the Image, 16 KiB aligned, with
its offset and size in the Image header's res2/res3 fields (0x20/0x28), as
iOS16-on-A40/loader/pack.py does: with header v4 ABL puts vendor_boot's
ramdisk in front of ours, so the Image is the only place whose layout we
control. Without --payload the shim boots into its diagnostic screen.

os_version / os_patch_level: copy them from your phone's stock boot.img with
--like. Do not make up a future patch level: the Titan M2 remembers the
highest one it was shown (docs/hardware.md).
"""
import argparse, gzip, struct, sys

PAGE = 4096
V4_HEADER = 1584


def elf_text(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[18] != 183:
        sys.exit(f'{path}: not a 64-bit aarch64 ELF')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3A)
    secs = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names = secs[shstrndx]
    name = lambda s: data[names[4] + s[0]:data.index(b'\0', names[4] + s[0])].decode()
    text = None
    for s in secs:
        n, typ, flags = name(s), s[1], s[2]
        if typ in (4, 9):
            sys.exit(f'relocation section {n}: the shim would not be position independent')
        if n == '.text':
            text = s
        elif typ in (1, 8) and flags & 2 and s[5]:
            sys.exit(f'unexpected allocated section {n}')
    if text is None:
        sys.exit('no .text')
    for s in secs:
        if s[1] == 2:
            strtab = secs[s[6]]
            for off in range(s[4], s[4] + s[5], 24):
                st_name, _, _, _, st_value, _ = struct.unpack_from('<IBBHQQ', data, off)
                sym = data[strtab[4] + st_name:data.index(b'\0', strtab[4] + st_name)]
                if sym == b'_head' and st_value != 0:
                    sys.exit('_head is not at offset 0')
    blob = bytearray(data[text[4]:text[4] + text[5]])
    if blob[0x38:0x3C] != b'ARM\x64':
        sys.exit('no arm64 Image header')
    return blob


def make_image(shim, payload):
    img = bytearray(shim)
    if payload:
        if payload[:8] != b'SWEETXNU':
            sys.exit('payload is not SWEETXNU (tools/mkpayload.py)')
        off = (len(img) + 0x3FFF) & ~0x3FFF
        img = img.ljust(off, b'\0') + payload
        struct.pack_into('<QQ', img, 0x20, off, len(payload))
    img += b'\0' * (-len(img) % 16)
    struct.pack_into('<Q', img, 0x10, len(img))	# image_size
    return bytes(img)


def empty_ramdisk():
    """A gzip'd newc cpio with only the trailer (some ABLs dislike size 0)."""
    name = b'TRAILER!!!\0'
    hdr = b'070701' + b''.join(b'%08X' % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0))
    cpio = hdr + name
    cpio += b'\0' * (-len(cpio) % 4)
    cpio += b'\0' * (-len(cpio) % 512)
    return gzip.compress(cpio, mtime=0)


def os_field(version, patch):
    """boot.img os_version: A.B.C << 11 | (Y - 2000) << 4 | M."""
    if not version:
        return 0
    a, b, c = (list(map(int, version.split('.'))) + [0, 0])[:3]
    y, m = map(int, patch.split('-')[:2])
    return (a << 25) | (b << 18) | (c << 11) | ((y - 2000) << 4) | m


def like_fields(path):
    h = open(path, 'rb').read(V4_HEADER)
    if h[:8] != b'ANDROID!':
        sys.exit(f'{path}: not an Android boot image')
    ver, = struct.unpack_from('<I', h, 40)
    if ver >= 3:
        osv, = struct.unpack_from('<I', h, 16)
        cmdline = h[44:44 + 1536].split(b'\0', 1)[0]
    else:
        osv, = struct.unpack_from('<I', h, 44)
        cmdline = h[64:576].split(b'\0', 1)[0]
    return osv, cmdline


def boot_v4(kernel, ramdisk, os_version, cmdline):
    hdr = b'ANDROID!' + struct.pack('<4I', len(kernel), len(ramdisk), os_version, V4_HEADER)
    hdr += b'\0' * 16 + struct.pack('<I', 4) + cmdline.ljust(1536, b'\0') + struct.pack('<I', 0)
    assert len(hdr) == V4_HEADER
    pad = lambda b: b + b'\0' * (-len(b) % PAGE)
    return pad(hdr) + pad(kernel) + pad(ramdisk)


def describe(osv):
    if not osv:
        return 'unset'
    v, p = osv >> 11, osv & 0x7ff
    return f'{v >> 14}.{(v >> 7) & 0x7f}.{v & 0x7f} patch {2000 + (p >> 4)}-{p & 0xf:02d}'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('shim')
    ap.add_argument('outdir')
    ap.add_argument('--payload')
    ap.add_argument('--like', help='stock boot.img to copy os_version/patch level from')
    ap.add_argument('--os-version', help='e.g. 14.0.0 (with --patch-level)')
    ap.add_argument('--patch-level', default='2024-01')
    ap.add_argument('--cmdline', default='', help='boot.img command line (ABL appends its own)')
    a = ap.parse_args()

    payload = open(a.payload, 'rb').read() if a.payload else None
    image = make_image(elf_text(a.shim), payload)
    osv, cmdline = os_field(a.os_version, a.patch_level), a.cmdline.encode()
    if a.like:
        osv, _ = like_fields(a.like)
    import os
    os.makedirs(a.outdir, exist_ok=True)
    open(os.path.join(a.outdir, 'Image'), 'wb').write(image)
    boot = boot_v4(image, empty_ramdisk(), osv, cmdline)
    open(os.path.join(a.outdir, 'boot.img'), 'wb').write(boot)
    print(f'Image {len(image)} bytes ({"payload " + str(len(payload)) if payload else "diagnostic, no payload"}), '
          f'boot.img v4 {len(boot)} bytes, os_version {describe(osv)}')
    if not a.like and not a.os_version:
        print('note: os_version unset; pass --like your stock boot.img for a real phone', file=sys.stderr)


if __name__ == '__main__':
    main()
