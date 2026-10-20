#!/usr/bin/env python3
"""Build the Apple Device Tree for the Pixel 6a (bluejay) fork of the SWEET xnu.

Google Pixel 6a: Google Tensor (GS101, Exynos-derived): 4 x Cortex-A55,
2 x Cortex-A76, 2 x Cortex-X1, all ARMv8.2 with the 16K granule; GIC-600
(GICv3); Exynos UFS host; Samsung (USI) UART; 1080x2400 panel scanned out
from the splash buffer ABL leaves at 0xfac00000.

  mkadt.py OUT.adt [--qemu] [--dump] [--x2]

--qemu: the same kernel on QEMU virt (gic-version=3, PL011, virtio-mmio
block device instead of UFS); see tools/qemu-bluejay.py.

Every hardware address is in one block below with its source and how sure we
are of it. The ones marked VERIFY have not been measured on a bluejay yet:
the shim's diagnostic boot (docs/bringup.md) prints what the
live device tree and registers say, so they can be fixed here without a
kernel rebuild (the kernel reads them from this tree).
"""
import hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

# ---------------------------------------------------------------- memory
# GS101 DRAM: 2 GiB at 0x80000000 (firmware carve-outs, ABL, our shim) and
# the rest at 0x880000000 (4 GiB on the 6 GiB Pixel 6a). XNU gets one
# contiguous range in the upper bank; the shim refuses to boot if the live
# FDT's /memory does not cover it or a fixed /reserved-memory region
# overlaps it. 3.5 GiB leaves the top 512 MiB alone until the live tree has
# been checked (raise it once docs/bringup.md step 2 shows it is free).
PHYS_BASE = 0x880000000
MEM_SIZE = 0xE0000000
# QEMU: same base (the harness gives virt enough RAM to reach it), 2 GiB
QEMU_MEM_SIZE = 0x80000000

# /arm-io children are relative to this; it must not be 0 (serial_init bails)
SOC_BASE = 0x01000000

# ---------------------------------------------------------------- GS101
# Mainline Linux arch/arm64/boot/dts/exynos/google/gs101.dtsi
GICD, GICD_SIZE = 0x10400000, 0x10000
GICR, GICR_SIZE = 0x10440000, 0x100000      # 8 redistributors x 0x20000
VTIMER_INTID = 27                           # PPI 11, the architected default
TIMEBASE = 24576000                         # gs101.dtsi timer clock-frequency
UART, UART_SIZE = 0x10a00000, 0x100         # serial_0 (USI), on the USB-C SBU pins
# UFS: "hci" and "vs_hci" regs of ufs@14700000 (google,gs101-ufs)
UFS, UFS_SIZE = 0x14700000, 0x200
UFS_VS, UFS_VS_SIZE = 0x14701100, 0x200

# Splash framebuffer (mainline gs101-oriole.dts simple-framebuffer, same
# panel size on bluejay). VERIFY on bluejay: the shim reports the live FDT's
# framebuffer / splash reservation and stops if they disagree.
FB = 0xfac00000
FB_W, FB_H, FB_STRIDE = 1080, 2400, 1080 * 4
FB_SIZE = 0xA00000

# gpio-keys, active low. Banks from mainline pinctrl-exynos-arm64.c
# (gs101_pin_alive / gs101_pin_far_alive, DAT = bank + 4, checked), pins from
# gs101-oriole.dts. VERIFY on bluejay: the shim's diagnostic boot shows the
# three DAT registers live while you press the buttons.
ALIVE, FAR_ALIVE = 0x174d0000, 0x174e0000
PERIC1_PINCTRL = 0x10c40000                   # gpp20-gpp27 (touch SPI, reset, select)
KEY_POWER, KEY_VOLUP, KEY_VOLDOWN = 0, 1, 2  # sweet_hid_key() indices
KEYS = [
    (ALIVE + 0xe0 + 4, 1, KEY_POWER),        # gpa10-1
    (FAR_ALIVE + 0x40 + 4, 1, KEY_VOLUP),    # gpa8-1
    (FAR_ALIVE + 0x20 + 4, 3, KEY_VOLDOWN),  # gpa7-3
]

# ---------------------------------------------------------------- QEMU virt
QEMU_GICD, QEMU_GICR, QEMU_GICR_SIZE = 0x08000000, 0x080a0000, 0x100000
QEMU_TIMEBASE = 62500000                     # cortex-a76 under TCG
QEMU_UART = 0x09000000
QEMU_VIRTIO, QEMU_VIRTIO_SIZE = 0x0a000000, 0x4000

ECID = 0x50363661_00000001                  # 'P66a' + 1, synthetic
BOOT_MANIFEST_HASH = hashlib.sha384(b'sweet boot manifest').digest()

# Real iPhone X tree for /product (see iOS16-on-A40 README); kept out of git.
D22AP_ADT = os.environ.get('D22AP_ADT', os.path.join(os.path.dirname(os.path.abspath(__file__)), '../ipsw/d22ap.adt'))

# Cores in MPIDR order: Aff1 = core number, like sweet (DynamIQ).
CORES = [('arm,cortex-a55', 1803000000)] * 4 + [('arm,cortex-a76', 2253000000)] * 2 + \
        [('arm,cortex-x1', 2802000000)] * 2

CANVAS = (1125, 2436)

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def d22ap_product(path=D22AP_ADT):
    """/product of the real D22AP tree, without iBoot's syscfg placeholders."""
    if not os.path.exists(path):
        print(f'warning: {path} missing, no /product node', file=sys.stderr)
        return None
    root, _ = adt.parse(open(path, 'rb').read())
    for c in root.children:
        if dict((k, v) for k, f, v in c.props).get('name') == b'product\0':
            return [(k, v) for k, f, v in c.props if not f and k not in ('name', 'AAPL,phandle')]
    raise SystemExit(f'{path}: no /product node')


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def build(seed=None, qemu=False):
    mem_size = QEMU_MEM_SIZE if qemu else MEM_SIZE
    gicd, gicr, gicr_size = (QEMU_GICD, QEMU_GICR, QEMU_GICR_SIZE) if qemu else (GICD, GICR, GICR_SIZE)
    timebase = QEMU_TIMEBASE if qemu else TIMEBASE
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    # /chosen: same meaning as in ios-sweet/tools/mkadt.py (comments there).
    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(PHYS_BASE)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('bluejay-shim')),
        ('system-firmware-version', cstr('bluejay-shim')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        ('display-scale', u32(3)),
        ('unique-chip-id', struct.pack('<Q', ECID)),
        ('chip-id', u32(0x8015)),
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # All eight cores, one homogeneous cluster (cpus=N starts a prefix).
    # QEMU keeps one CPU: virt's MPIDRs do not follow the Aff1 = core layout.
    cpu_nodes = []
    for core in range(1 if qemu else len(CORES)):
        compat, hz = CORES[core]
        cpu_nodes.append(node(f'cpu{core}', [
            ph(),
            ('device_type', cstr('cpu')),
            # SweetCPU attaches through IONameMatch 'qcom,kryo470'
            # (iokit/KernelConfigTables.cpp): without it no CPU starts.
            ('compatible', cstr('qcom,kryo470') + cstr('google,gs101') + cstr(compat) + cstr('ARM,v8')),
            ('reg', u32(core << 8)),        # MPIDR Aff1:Aff0
            ('cpu-id', u32(core)),
            ('cluster-id', u32(0)),
            ('cluster-type', u32(0)),
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(core)),
            ('l2-cache-id', u32(core)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if core == 0 else 'stopped')),
            ('timebase-frequency', u32(timebase)),
            ('clock-frequency', u32(hz)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(1866000000)),     # keep below 2^31: read as int
            ('peripheral-frequency', u32(timebase)),
            ('fixed-frequency', u32(timebase)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)), ('#size-cells', u32(0))], cpu_nodes)

    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-v3')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('reg', u64(gicd - SOC_BASE, 0x10000, gicr - SOC_BASE, gicr_size)),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('sweet-io')),
        ('compatible', cstr('sweet-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('sweet,display')),
            ('canvas-width', u32(CANVAS[0])),
            ('canvas-height', u32(CANVAS[1])),
        ])])

    # Video-mode scanout: no "trigger" property (A40's DECON needed one).
    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    # Marker for the kernel: not the Qualcomm sweet, keep its SPMI/GENI/RPMh/
    # USB drivers off the bus (bsd/dev/a40_keys.c a40_platform()).
    board = node('bluejay', [ph(), ('compatible', cstr('google,bluejay')), ('qemu', u32(1 if qemu else 0))])

    # Polled TX-only console (pexpert/arm/pe_serial.c sweet_uart). Absolute
    # physical address. The phone's UART is only touched with sweetuart=1,
    # which the shim adds when ABL enabled the port (fastboot oem uart enable).
    uart = node('sweet-uart', [
        ph(),
        ('compatible', cstr('arm,pl011') if qemu else cstr('samsung,exynos4210-uart')),
        ('reg', u64(QEMU_UART if qemu else UART, 0x1000 if qemu else UART_SIZE)),
    ])

    if qemu:
        storage = node('a40-virtio', [ph(), ('compatible', cstr('virtio,mmio')),
                                      ('reg', u64(QEMU_VIRTIO, QEMU_VIRTIO_SIZE))])
    else:
        # bsd/dev/sweet_ufs.c, Exynos flavour: byte-granular UTRD offsets,
        # PRDT segments of at most 4 KiB, PRDT entry size, LUN 0 first.
        storage = node('sweet-ufs', [
            ph(),
            ('compatible', cstr('google,gs101-ufs') + cstr('samsung,exynos-ufs')),
            ('reg', u64(UFS, UFS_SIZE)),
            ('vs-hci', u64(UFS_VS, UFS_VS_SIZE)),
            ('exynos', u32(1)),
            ('prdt-entry-size', u32(16)),
            ('lun', u32(0)),
        ])

    # bsd/dev/a40_keys.c generic path: <u64 DAT register PA, u32 bit, u32 key>,
    # active low. Bit 31 of the bit field would mark an active-high input.
    keys = [] if qemu else [node('gpio-keys', [
        ph(),
        ('keys', b''.join(struct.pack('<QII', pa, bit, key) for pa, bit, key in KEYS)),
    ])]

    # bsd/dev/bluejay_touch.c: ST FTM5 on spi_11, bit-banged on its GPIOs
    # (Google gs101-common-touch.dtsi; bank offsets from mainline pinctrl).
    # spi = <GPP23 bank, clk, mosi, miso, cs>, ctl = <GPP26 bank, reset, select>
    touch = [] if qemu else [node('bluejay-touch', [
        ph(),
        ('compatible', cstr('st,fts')),
        ('spi', struct.pack('<Q4I', PERIC1_PINCTRL + 0x60, 0, 1, 2, 3)),
        ('ctl', struct.pack('<Q2I', PERIC1_PINCTRL + 0xc0, 0, 1)),
        ('size', u32(FB_W - 1, FB_H - 1)),
    ])]

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(0)),     # all three on the right edge
    ])

    product_props = d22ap_product()
    if product_props and CANVAS != (1125, 2436):
        over = {'artwork-scale-factor': u32(2), 'artwork-device-subtype': u32(1792)}
        product_props = [(k, over.get(k, v)) for k, v in product_props]
    product = [node('product', [ph()] + product_props)] if product_props else []

    # compatible[0]/model: the iPhone X identity of the userland; 'xiaomi,sweet'
    # keeps SweetPlatformExpert matching.
    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('iPhone10,3') + cstr('xiaomi,sweet') + cstr('google,bluejay') + cstr('AppleARM')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons, board, uart] + product + [storage] + keys + touch)


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        raise SystemExit(__doc__)
    if '--x2' in sys.argv:
        CANVAS = (828, 1792)
    blob = adt.serialize(build(qemu='--qemu' in sys.argv))
    open(args[0], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{args[0]}: {len(blob)} bytes', file=sys.stderr)
