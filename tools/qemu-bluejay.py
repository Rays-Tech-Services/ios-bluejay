#!/usr/bin/env python3
"""Boot the Pixel 6a shim (and the XNU payload inside it) in QEMU.

  qemu-bluejay.py IMAGE OUTDIR [--secs N] [--disk root.img] [--el1] [--gdb]
                  [--watch S] [--initrd-payload] [--fail-on-panic] [--xnu-src DIR]

IMAGE is out/.../Image from tools/mkbootimg.py: the very file that goes into
the phone's boot.img. QEMU plays ABL: it puts the Image in the low DRAM bank,
hands it a flattened device tree shaped like the Pixel's (two DRAM banks at
0x80000000 and 0x880000000, firmware reserved-memory, the splash framebuffer
at 0xfac00000, PSCI) and enters it at EL2 like Pixel ABL does (--el1: enter
at EL1 instead). The board is QEMU virt with GICv3, so the payload must be
built with an ADT from `mkadt.py --qemu` (PL011 console, virtio-blk root).

Outputs in OUTDIR:
  serial.txt  PL011 output (shim log, and the kernel log with sweetuart)
  fb.png      the 1080x2400 panel at the end (and every --watch S seconds)
  fb.txt      framebuffer text (shim font always; XNU's with --xnu-src/--font)
  regs.txt    CPU state at the end; qemu.log guest errors / exceptions
  result.json status: shim_halted / panic / running / qemu_exited

--initrd-payload moves the payload out of the Image into an initrd behind a
fake vendor ramdisk, to test the shim's fallback search.
"""
import argparse, json, math, os, re, shutil, socket, struct, subprocess, sys, time, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FB_BASE, FB_W, FB_H = 0xfac00000, 1080, 2400
FB_STRIDE = FB_W * 4
LOW_BASE, LOW_SIZE = 0x80000000, 0x80000000
HIGH_BASE = 0x880000000
IMAGE_PA = 0x80080000          # 2 MiB + 512 KiB, like a Linux text_offset load
DTB_PA = 0x8f000000
INITRD_PA = 0x90000000
STUB_PA = 0x40200000           # virt RAM starts at 0x40000000
VIRT_RAM = 0x40000000
UART_PA = 0x09000000


# ---------------------------------------------------------------- FDT writer

class FDT:
    """Minimal flattened device tree builder (no dtc needed)."""

    def __init__(self):
        self.struct, self.strings, self.offs = bytearray(), bytearray(), {}

    def _str(self, s):
        if s not in self.offs:
            self.offs[s] = len(self.strings)
            self.strings += s.encode() + b'\0'
        return self.offs[s]

    def begin(self, name):
        self.struct += struct.pack('>I', 1) + name.encode() + b'\0'
        self.struct += b'\0' * (-len(self.struct) % 4)

    def end(self):
        self.struct += struct.pack('>I', 2)

    def prop(self, name, value=b''):
        if isinstance(value, str):
            value = value.encode() + b'\0'
        elif isinstance(value, int):
            value = struct.pack('>I', value)
        elif isinstance(value, (list, tuple)):
            value = b''.join(v.encode() + b'\0' for v in value)
        self.struct += struct.pack('>III', 3, len(value), self._str(name)) + value
        self.struct += b'\0' * (-len(self.struct) % 4)

    def blob(self):
        self.struct += struct.pack('>I', 9)
        rsv = b'\0' * 16
        off_rsv = 40
        off_struct = off_rsv + len(rsv)
        off_strings = off_struct + len(self.struct)
        total = off_strings + len(self.strings)
        hdr = struct.pack('>10I', 0xd00dfeed, total, off_struct, off_strings, off_rsv, 17, 16, 0,
                          len(self.strings), len(self.struct))
        return hdr + rsv + bytes(self.struct) + bytes(self.strings)


def q(*v):
    return b''.join(struct.pack('>Q', x) for x in v)


def pixel_like_fdt(high_size, bootargs, initrd=None, smp=1):
    """What Pixel ABL hands the kernel, reduced to what the shim reads."""
    f = FDT()
    f.begin('')
    f.prop('#address-cells', 2)
    f.prop('#size-cells', 2)
    f.prop('model', 'QEMU stand-in for Google Pixel 6a (bluejay)')
    f.prop('compatible', ['google,gs101-bluejay', 'google,gs101'])

    f.begin('chosen')
    f.prop('#address-cells', 2)
    f.prop('#size-cells', 2)
    f.prop('ranges')
    f.prop('bootargs', bootargs)
    f.prop('stdout-path', '/pl011@9000000')
    if initrd:
        f.prop('linux,initrd-start', q(initrd[0]))
        f.prop('linux,initrd-end', q(initrd[1]))
    f.begin(f'framebuffer@{FB_BASE:x}')
    f.prop('compatible', 'simple-framebuffer')
    f.prop('reg', q(FB_BASE, FB_STRIDE * FB_H))
    f.prop('width', FB_W)
    f.prop('height', FB_H)
    f.prop('stride', FB_STRIDE)
    f.prop('format', 'a8r8g8b8')
    f.end()
    f.end()

    f.begin(f'memory@{LOW_BASE:x}')
    f.prop('device_type', 'memory')
    f.prop('reg', q(LOW_BASE, LOW_SIZE, HIGH_BASE, high_size))
    f.end()

    # Firmware carve-outs live in the low bank on GS101; a few stand-ins so the
    # shim's overlap check has something to look at.
    f.begin('reserved-memory')
    f.prop('#address-cells', 2)
    f.prop('#size-cells', 2)
    f.prop('ranges')
    for name, base, size in (('el3_mon', 0x88800000, 0x200000), ('tee', 0x90200000, 0x2000000),
                             ('splash', FB_BASE, FB_STRIDE * FB_H)):
        f.begin(f'{name}@{base:x}')
        f.prop('reg', q(base, size))
        f.prop('no-map')
        f.end()
    f.end()

    f.begin('cpus')
    f.prop('#address-cells', 2)
    f.prop('#size-cells', 0)
    for i in range(smp):
        f.begin(f'cpu@{i}')
        f.prop('device_type', 'cpu')
        f.prop('compatible', 'arm,cortex-a76')
        f.prop('reg', q(i))
        f.prop('enable-method', 'psci')
        f.end()
    f.end()

    f.begin('psci')
    f.prop('compatible', ['arm,psci-1.0', 'arm,psci-0.2'])
    f.prop('method', 'smc')
    f.end()

    f.begin(f'pl011@{UART_PA:x}')
    f.prop('compatible', ['arm,pl011', 'arm,primecell'])
    f.prop('reg', q(UART_PA, 0x1000))
    f.end()
    f.end()
    return f.blob()


# ---------------------------------------------------------------- QEMU control

def qmp(sock_path, *cmds, pmemsave=None, stop=False):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    fh = s.makefile('rw')
    fh.readline()

    def call(execute, **args):
        fh.write(json.dumps({'execute': execute, 'arguments': args}) + '\n')
        fh.flush()
        while True:
            r = json.loads(fh.readline())
            if 'return' in r or 'error' in r:
                return r

    call('qmp_capabilities')
    if stop:
        call('stop')
    out = []
    for c in cmds:
        r = call('human-monitor-command', **{'command-line': c})
        out.append(r.get('return', '') if 'return' in r else f'error: {r["error"]}')
    if pmemsave:
        r = call('pmemsave', val=pmemsave[0], size=pmemsave[1], filename=pmemsave[2])
        if 'error' in r:
            print(f'qmp pmemsave: {r["error"]}', file=sys.stderr)
    s.close()
    return out


def write_png(path, raw):
    rows = []
    for y in range(FB_H):
        line = raw[y * FB_STRIDE:(y + 1) * FB_STRIDE]
        rgb = bytearray(FB_W * 3)		# a8r8g8b8 = B G R A in memory
        rgb[0::3], rgb[1::3], rgb[2::3] = line[2::4], line[1::4], line[0::4]
        rows.append(b'\0' + bytes(rgb))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', FB_W, FB_H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


# ---------------------------------------------------------------- screen text

def c_font(path, name):
    """16-byte glyph rows from a C array initializer (shim/font.h, xnu iso_font.c)."""
    src = open(path).read()
    body = src[src.index(name):]
    body = body[body.index('{') + 1:]
    nums = [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', re.sub(r'/\*.*?\*/', '', body.split('};')[0], flags=re.S))]
    return bytes(nums)


def decode(raw, font, first, scale, msb_left):
    """Exact glyph matching; 'on' = differs from the cell's top-left pixel."""
    glyphs = {}
    for i in range(len(font) // 16):
        if 0x21 <= first + i <= 0x7e:
            glyphs.setdefault(font[i * 16:(i + 1) * 16], chr(first + i))
    glyphs[bytes(16)] = ' '
    cw, ch = 8 * scale, 16 * scale
    rows = []
    for r in range(FB_H // ch):
        line = []
        for c in range(FB_W // cw):
            y0, x0 = r * ch, c * cw
            bg = raw[y0 * FB_STRIDE + x0 * 4:y0 * FB_STRIDE + x0 * 4 + 4]
            g = bytearray(16)
            for gy in range(16):
                off = (y0 + gy * scale) * FB_STRIDE + x0 * 4
                for gx in range(8):
                    px = raw[off + gx * scale * 4:off + gx * scale * 4 + 4]
                    if px != bg:
                        g[gy] |= (0x80 >> gx) if msb_left else (1 << gx)
            line.append(glyphs.get(bytes(g), '?' if any(g) else ' '))
        rows.append(''.join(line).rstrip())
    text = '\n'.join(rows).strip('\n')
    known = sum(ch not in ' ?' for ch in text)
    return text, known


def status_of(text):
    if re.search(r'panic\(cpu|SWEET EARLY PANIC|EXCEPTION vec|Kernel panic', text):
        return 'panic'
    if 'stopped. Power' in text:
        return 'shim_halted'
    if 'DIAGNOSTIC BOOT' in text:
        return 'diagnostic'
    return 'running'


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', nargs='?')
    ap.add_argument('outdir', nargs='?')
    ap.add_argument('--secs', type=float, default=30)
    ap.add_argument('--disk', help='raw root disk (GPT with a "userdata" partition) as virtio-blk')
    ap.add_argument('--snapshot', action='store_true', help='do not write to --disk')
    ap.add_argument('--el1', action='store_true', help='enter at EL1 instead of EL2')
    ap.add_argument('--smp', type=int, default=1)
    ap.add_argument('--cpu', default='cortex-a76')
    ap.add_argument('--high-gib', type=int, default=4, help='size of the 0x880000000 bank (default 4)')
    ap.add_argument('--bootargs', default='console=ttySAC0,115200 androidboot.hardware=bluejay',
                    help='what ABL would pass in /chosen/bootargs (add shimdiag for the diagnostic screen)')
    ap.add_argument('--initrd-payload', action='store_true')
    ap.add_argument('--watch', type=float, default=0, help='save fb.png every S seconds while running')
    ap.add_argument('--xnu-src', help='xnu source tree, to decode the kernel console (osfmk/console/iso_font.c)')
    ap.add_argument('--gdb', action='store_true', help='start halted with a gdb stub on :1234')
    ap.add_argument('--trace-int', action='store_true')
    ap.add_argument('--hmp', action='append', default=[], help='monitor command run at the end')
    ap.add_argument('--fail-on-panic', action='store_true', help='exit 2 on panic, 3 if the shim halted')
    ap.add_argument('--check-env', action='store_true')
    a = ap.parse_args()

    qemu = shutil.which('qemu-system-aarch64')
    if a.check_env or not qemu:
        print(f'qemu-system-aarch64: {qemu or "MISSING (brew install qemu / apt install qemu-system-arm)"}', file=sys.stderr)
        return 0 if qemu else 1
    if not a.image or not a.outdir:
        ap.error('IMAGE and OUTDIR are required')
    os.makedirs(a.outdir, exist_ok=True)
    out = os.path.abspath(a.outdir)
    for f in ('serial.txt', 'fb.png', 'fb.txt', 'result.json', 'regs.txt', 'qemu.log'):
        if os.path.exists(f'{out}/{f}'):
            os.unlink(f'{out}/{f}')

    image = bytearray(open(a.image, 'rb').read())
    if image[0x38:0x3c] != b'ARM\x64':
        sys.exit(f'{a.image}: not an arm64 Image')
    initrd = None
    if a.initrd_payload:
        off, size = struct.unpack_from('<QQ', image, 0x20)
        if not size:
            sys.exit('--initrd-payload: the Image has no payload')
        payload = bytes(image[off:off + size])
        image = image[:off]
        struct.pack_into('<QQQ', image, 0x10, len(image), 0, 0)
        vendor = os.urandom(0x123456)		# stand-in for vendor_boot's ramdisk
        rd = vendor + b'\0' * (-len(vendor) % 0x1000) + payload
        open(f'{out}/initrd.bin', 'wb').write(rd)
        initrd = (INITRD_PA, INITRD_PA + len(rd))
    open(f'{out}/Image', 'wb').write(image)
    if IMAGE_PA + len(image) > DTB_PA:
        sys.exit('Image too large for the low-bank layout')

    high = a.high_gib << 30
    open(f'{out}/bluejay.dtb', 'wb').write(pixel_like_fdt(high, a.bootargs, initrd, a.smp))
    # ldr x0, =dtb ; ldr x1, =image ; br x1 ; nop ; .quad dtb ; .quad image
    open(f'{out}/stub.bin', 'wb').write(struct.pack('<4I2Q', 0x58000080, 0x580000a1, 0xd61f0020,
                                                    0xd503201f, DTB_PA, IMAGE_PA))
    ram_gib = math.ceil((HIGH_BASE + high - VIRT_RAM) / (1 << 30))

    mon = f'{out}/mon.sock'
    if os.path.exists(mon):
        os.unlink(mon)
    machine = 'virt,gic-version=3,its=off,highmem=on' + ('' if a.el1 else ',virtualization=on')
    cmd = [qemu, '-M', machine, '-cpu', a.cpu, '-smp', str(a.smp), '-m', f'{ram_gib}G',
           '-nographic', '-serial', f'file:{out}/serial.txt', '-monitor', 'none',
           '-qmp', f'unix:{mon},server,nowait', '-no-reboot',
           '-d', ('int,' if a.trace_int else '') + 'guest_errors,unimp', '-D', f'{out}/qemu.log',
           '-global', 'virtio-mmio.force-legacy=true',
           '-device', f'loader,file={out}/Image,addr={IMAGE_PA:#x},force-raw=on',
           '-device', f'loader,file={out}/bluejay.dtb,addr={DTB_PA:#x},force-raw=on',
           '-device', f'loader,file={out}/stub.bin,addr={STUB_PA:#x},force-raw=on',
           '-device', f'loader,addr={STUB_PA:#x},cpu-num=0']
    if initrd:
        cmd += ['-device', f'loader,file={out}/initrd.bin,addr={INITRD_PA:#x},force-raw=on']
    if a.disk:
        cmd += ['-drive', f'if=none,file={a.disk},format=raw,id=d0,cache=unsafe', '-device', 'virtio-blk-device,drive=d0']
        if a.snapshot:
            cmd += ['-snapshot']
    if a.gdb:
        cmd += ['-S', '-gdb', 'tcp::1234']
    print(f'QEMU: {ram_gib} GiB, entry {"EL1" if a.el1 else "EL2"}, Image at {IMAGE_PA:#x}, DTB at {DTB_PA:#x}'
          + (f', initrd {initrd[0]:#x}-{initrd[1]:#x}' if initrd else ''), file=sys.stderr)

    p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL)
    try:
        if a.gdb:
            print('gdb stub on :1234 (target remote :1234)', file=sys.stderr)
            p.wait()
            return 0
        t0 = last = time.time()
        while p.poll() is None and time.time() - t0 < a.secs:
            time.sleep(0.25)
            if a.watch and time.time() - last >= a.watch and os.path.exists(mon):
                last = time.time()
                qmp(mon, pmemsave=(FB_BASE, FB_STRIDE * FB_H, f'{out}/fb.raw'))
                if os.path.exists(f'{out}/fb.raw'):
                    write_png(f'{out}/fb.png', open(f'{out}/fb.raw', 'rb').read())
        result = {'status': 'qemu_exited', 'qemu_returncode': p.returncode}
        if p.poll() is None:
            regs, *extra = qmp(mon, 'info registers', *a.hmp, stop=True,
                               pmemsave=(FB_BASE, FB_STRIDE * FB_H, f'{out}/fb.raw'))
            open(f'{out}/regs.txt', 'w').write(regs + ''.join(f'\n(qemu) {c}\n{r}' for c, r in zip(a.hmp, extra)))
            p.kill()
            p.wait()
            raw = open(f'{out}/fb.raw', 'rb').read()
            os.unlink(f'{out}/fb.raw')
            write_png(f'{out}/fb.png', raw)
            texts = []
            shim_font = c_font(os.path.join(HERE, '../shim/font.h'), 'font8x16')
            texts.append(decode(raw, shim_font, 0x20, 2, True))
            if a.xnu_src:
                iso = c_font(os.path.join(a.xnu_src, 'osfmk/console/iso_font.c'), 'iso_font')
                texts.append(decode(raw, iso, 0, 2, False))
            screen = max(texts, key=lambda t: t[1])[0]
            open(f'{out}/fb.txt', 'w').write(screen + '\n')
            serial = open(f'{out}/serial.txt', errors='replace').read() if os.path.exists(f'{out}/serial.txt') else ''
            result = {'status': status_of(serial + '\n' + screen), 'secs': a.secs,
                      'serial_bytes': len(serial)}
        open(f'{out}/result.json', 'w').write(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), file=sys.stderr)
        print(f'{out}/serial.txt  {out}/fb.png  {out}/fb.txt', file=sys.stderr)
        if a.fail_on_panic and result['status'] == 'panic':
            return 2
        if a.fail_on_panic and result['status'] == 'shim_halted':
            return 3
        return 1 if result['status'] == 'qemu_exited' else 0
    finally:
        if p.poll() is None:
            p.kill()


if __name__ == '__main__':
    sys.exit(main())
