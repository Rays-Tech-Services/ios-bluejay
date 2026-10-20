#!/usr/bin/env python3
"""Extract the payload of an IM4P file (e.g. DeviceTree.d22ap.im4p -> d22ap.adt).

  im4p.py IN.im4p OUT

An IM4P is DER: SEQUENCE { "IM4P", type, description, OCTET STRING payload,
[compression info] }. LZSS ("complzss") and LZFSE ("bvx2", through macOS
libcompression) payloads are decompressed. Encrypted payloads (KBAG) are
not supported; device trees in iOS 16 IPSWs are not encrypted.
Replaces `ipsw img4 im4p extract` for the README steps.
"""
import ctypes, struct, sys


def der(buf, off):
    """(tag, value_start, value_end) of the DER element at off."""
    tag = buf[off]
    n = buf[off + 1]
    off += 2
    if n & 0x80:
        k = n & 0x7f
        n = int.from_bytes(buf[off:off + k], 'big')
        off += k
    return tag, off, off + n


def payload(buf):
    tag, start, end = der(buf, 0)
    assert tag == 0x30, 'not a DER SEQUENCE'
    items, off = [], start
    while off < end:
        t, s, e = der(buf, off)
        items.append((t, buf[s:e]))
        off = e
    assert items[0] == (0x16, b'IM4P'), 'not an IM4P'
    print(f'type {items[1][1].decode()}, {items[2][1].decode()!r}', file=sys.stderr)
    data = next(v for t, v in items[3:] if t == 0x04)
    return data


def lzss(src, size):
    """Apple 'complzss' (4 KiB window, 18-byte max match)."""
    N, F, T = 4096, 18, 2
    text = bytearray(b' ' * (N - F)) + bytearray(F)
    r, out, i, flags = N - F, bytearray(), 0, 0
    while i < len(src) and len(out) < size:
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xff00
            i += 1
        if flags & 1:
            c = src[i]
            i += 1
            out.append(c)
            text[r] = c
            r = (r + 1) & (N - 1)
        else:
            if i + 1 >= len(src):
                break
            p = src[i] | ((src[i + 1] & 0xf0) << 4)
            n = (src[i + 1] & 0x0f) + T
            i += 2
            for k in range(n + 1):
                c = text[(p + k) & (N - 1)]
                out.append(c)
                text[r] = c
                r = (r + 1) & (N - 1)
    return bytes(out[:size])


def lzfse(src):
    lib = ctypes.CDLL('/usr/lib/libcompression.dylib')
    size = len(src) * 16
    while True:
        dst = ctypes.create_string_buffer(size)
        n = lib.compression_decode_buffer(dst, ctypes.c_size_t(size), src, ctypes.c_size_t(len(src)),
                                          None, 0x801)	# COMPRESSION_LZFSE
        if n == 0:
            raise SystemExit('LZFSE decode failed')
        if n < size:
            return dst.raw[:n]
        size *= 4


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    data = payload(open(sys.argv[1], 'rb').read())
    if data[:8] == b'complzss':
        size, = struct.unpack_from('>I', data, 12)
        data = lzss(data[0x180:], size)
    elif data[:4] == b'bvx2':
        data = lzfse(data)
    open(sys.argv[2], 'wb').write(data)
    print(f'{sys.argv[2]}: {len(data)} bytes', file=sys.stderr)


if __name__ == '__main__':
    main()
