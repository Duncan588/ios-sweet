#!/usr/bin/env python3
"""Build the Apple Device Tree for the Xiaomi 11 (venus, SM8350) XNU path.

Reuses tools/mkadt.py (identical D22AP identity policy per ADR-0007,
chip-id 0x8015) and only differs where the hardware differs:

- RAM: three discrete banks from the live Linux DT /memory on the phone
  (12 GB device; NOT a contiguous region like sweet's, and no attempt to
  bridge the modem/modemtz carve-out gap between them):
      0x80000000 + 0x37500000   ( 885 MB)
      0xC0000000 + 0x140000000  (5120 MB, up to 0x100000000 + 0x?)
      0x200000000 + 0x180000000 (6144 MB)
  Exposed as a /memory node with reg = <base, size> pairs (real Apple ADT
  form) so xnu pmap bank support can pick it up later; /chosen keeps the
  classic dram-base/dram-size pointing at the first bank, which is what
  tools/mkpayload.py and the shim loader read today.
- Display: QHD cmd-mode panel, canvas 1440x3200 (splash_region
  0xE2A00000+0x2300000, S1-verified). display-scale stays 3 => 480x1067 pt
  (iPhone 13-class); pt size is an S3 experiment parameter, not a guess to
  be tuned here (S3 0061: status bar is not sensitive to pt size).
- Identity: same iPhone10,3 / D22AP as sweet (userland requirement).

Usage: mkadt_venus.py OUT.adt [--dump].  Writes shim/out/venus-adt.bin by
default.  Runs on plain CPython 3.11, byte-identical on Windows and Linux:
the /chosen random-seed is a FIXED byte string (not os.urandom like
mkadt.py) so the artifact hashes the same on both build hosts; treat it as
a build constant until something needs real entropy.
"""
import os, struct, sys, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt, mkadt  # noqa: E402  (reuse: node(), d22ap_product(), serialize via adt)

# Live-machine RAM banks (Linux DT /memory, /sys/firmware/devicetree/base),
# dual-u64 <base size> pairs.
VENUS_RAM_BANKS = [
    (0x80000000, 0x37500000),
    (0xC0000000, 0x140000000),
    (0x200000000, 0x180000000),
]
# Display geometry (S1-verified ABL splash_region; QHD cmd mode panel).
VENUS_FB_W, VENUS_FB_H = 1440, 3200
VENUS_FB_STRIDE = VENUS_FB_W * 4
VENUS_FB_BASE, VENUS_FB_SIZE = 0xE2A00000, 0x2300000

DEFAULT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           '../shim/out/venus-adt.bin')
# Fixed /chosen random-seed (bytes(range(64))) so Windows/Linux builds agree.
FIXED_SEED = bytes(range(64))


def rng():
    return FIXED_SEED


def set_prop(n, name, value):
    for i, (k, f, _) in enumerate(n.props):
        if k == name:
            n.props[i] = (k, f, value)
            return
    n.props.append((name, 0, value))


def build():
    # mkadt.build() draws the random seed with os.urandom; pin it so the
    # artifact is hash-identical across build hosts.
    mkadt.os.urandom = lambda n: FIXED_SEED[:n]
    root = mkadt.build(qemu=False)
    chosen = root.children[0]

    # First bank below 4G is the primary kernel-managed region for the
    # current boot-args path (loader.c/mkpayload.py); the full bank table
    # lives in /memory for the pmap-bank experiment.
    set_prop(chosen, 'dram-base', mkadt.u64(VENUS_RAM_BANKS[0][0]))
    set_prop(chosen, 'dram-size', mkadt.u64(VENUS_RAM_BANKS[0][1]))
    stem = chosen.prop('firmware-version').rstrip(b'\0').decode()
    set_prop(chosen, 'firmware-version', mkadt.cstr('venus-' + stem))
    set_prop(chosen, 'system-firmware-version', mkadt.cstr('venus-' + stem))

    arm_io = next(c for c in root.children if c.name == 'arm-io')
    set_prop(arm_io, 'device_type', mkadt.cstr('venus-io'))
    set_prop(arm_io, 'compatible', mkadt.cstr('venus-io') + mkadt.cstr('arm-io'))
    disp0 = next(c for c in arm_io.children if c.name == 'disp0')
    # QHD canvas: IOMobileFramebuffer reads these (see SweetFramebuffer.cpp);
    # with /chosen display-scale 3 this presents as 480x1067 pt.
    set_prop(disp0, 'canvas-width', mkadt.u32(VENUS_FB_W))
    set_prop(disp0, 'canvas-height', mkadt.u32(VENUS_FB_H))

    fbcon = next(c for c in root.children if c.name == 'sweet-fbcon')
    set_prop(fbcon, 'reg', mkadt.u64(VENUS_FB_BASE, VENUS_FB_SIZE))
    set_prop(fbcon, 'width', mkadt.u32(VENUS_FB_W))
    set_prop(fbcon, 'height', mkadt.u32(VENUS_FB_H))
    set_prop(fbcon, 'stride', mkadt.u32(VENUS_FB_STRIDE))

    # Root identity: same consumer identity, SoC/part strings per SM8350.
    compat = chosen and None  # compatible lives on the root
    set_prop(root, 'compatible',
             mkadt.cstr('D22AP') + mkadt.cstr('iPhone10,3')
             + mkadt.cstr('xiaomi,venus') + mkadt.cstr('qcom,sm8350')
             + mkadt.cstr('AppleARM'))

    # /memory with the full bank table (Apple ADT reg form: u64 pairs).
    memory = mkadt.node('memory', [
        ('reg', mkadt.u64(*(x for bank in VENUS_RAM_BANKS for x in bank))),
    ])
    # Insert right after /chosen; the cpus/arm-io order is untouched.
    root.children.insert(1, memory)
    return root


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out', nargs='?', default=DEFAULT_OUT)
    ap.add_argument('--dump', action='store_true')
    args = ap.parse_args()
    root = build()
    blob = adt.serialize(root)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    open(args.out, 'wb').write(blob)
    if args.dump:
        adt.dump(adt.parse(blob)[0])
    print(f'{args.out}: {len(blob)} bytes', file=sys.stderr)


if __name__ == '__main__':
    main()
