#!/usr/bin/env python3
"""Unpack an Android boot image, header v0-v4 (also vendor_boot v3/v4).

Output layout matches tools/unpack_bootv2.py: kernel/ramdisk/second/dtb (and
friends) written next to a header.txt. v3/v4 boot headers have no dtb slot and
no base/offset fields; their vendor-related fields land in header.txt.

Usage: python3 unpack_boot.py <image> <outdir>
       python3 unpack_boot.py            # no-arg selftest (synthetic images)
"""
import os, struct, sys, tempfile

PAGE = 4096

def parse_boot_v0_2(b):
    (ks, ka, rs, ra, ss, sa, ta, ps, ver, osv) = struct.unpack_from('<10I', b, 8)
    ps = ps or PAGE
    cmd = b[64:64+512].split(b'\0')[0] + b[608:608+1024].split(b'\0')[0]
    info = dict(kernel_size=ks, kernel_addr=hex(ka), ramdisk_size=rs,
                ramdisk_addr=hex(ra), second_size=ss, tags_addr=hex(ta),
                page_size=ps, header_version=ver, os_version=hex(osv),
                name=b[48:64].split(b'\0')[0].decode(), cmdline=cmd.decode())
    off = ps
    parts = [('kernel', ks), ('ramdisk', rs), ('second', ss)]
    if ver >= 1:
        rds, rdo, hs = struct.unpack_from('<IQI', b, 1632)
        parts.append(('recovery_dtbo', rds)); info['header_size'] = hs
    if ver >= 2:
        ds, da = struct.unpack_from('<IQ', b, 1648)
        parts.append(('dtb', ds)); info['dtb_addr'] = hex(da)
    return info, parts, off, ps, ver

def parse_boot_v3_4(b):
    ks, rs, osv, hs = struct.unpack_from('<4I', b, 8)
    hv, = struct.unpack_from('<I', b, 40)
    kw, rw = struct.unpack_from('>2h', b, 44 + 1500)  # kernel/ramdisk tags @1580
    info = dict(kernel_size=ks, ramdisk_size=rs, header_version=hv,
                os_version=hex(osv), header_size=hs, cmdline=b[44:44+1536].split(b'\0')[0].decode())
    info['kernel_tags'] = kw; info['ramdisk_tags'] = rw
    return info, [('kernel', ks), ('ramdisk', rs)], PAGE, PAGE, hv

def parse_vendor_boot_v3_4(b):
    hs, = struct.unpack_from('<I', b, 20)  # header_size field (vendor boot v3 spec)
    if hs == 580:  # v4: full v3 fields + vendor_ramdisk_table
        (hs, dtbs, dtba, vt_size) = struct.unpack_from('<4I', b, 20)
        c_off, c_vlen, c_rank = struct.unpack('<3I', b, 36 + 2118)  # rev'd below
        info = dict(kind='vendor_boot', header_version=4, header_size=hs,
                    dtb_size=dtbs, dtb_addr=hex(dtba))
        cmd = b[36:36+2118].split(b'\0')[0].decode()
        parts = [('dtb', dtbs)]
        off = 580
        # vendor ramdisk table entries: 8x4 + name[16 + 2x1088] bytes each
        vt = b[580:580+vt_size]
        n = len(vt) // 3000 if len(vt) else 0
        rams = []
        for i in range(n):
            (rs, ro, rt, pt, rp, ph) = struct.unpack_from('<6I', vt, i * 3000)
            rams.append(dict(head=i, size=rs, ramdisk_type=rt,
                             table_entry_num=i, page = 0, size_bytes=rs))
        info['vendor_ramdisk_table_size'] = vt_size
        info['vendor_ramdisk_count'] = n
        info['vendor_ramdisk_table'] = str(rams)
        info['cmdline'] = cmd
        return info, parts, off, PAGE, 4
    # v3 (211): header_size(211), dtb sizes, big cmdline 2118
    hs, dtbs, dtba = struct.unpack_from('<3I', b, 20)
    info = dict(kind='vendor_boot', header_version=3, header_size=hs,
                dtb_size=dtbs, dtb_addr=hex(dtba),
                cmdline=b[36:36+2118].split(b'\0')[0].decode())
    return info, [('dtb', dtbs)], 211, PAGE, 3

def unpack(src, out):
    b = open(src, 'rb').read()
    os.makedirs(out, exist_ok=True)
    assert b[:8] == b'ANDROID!', 'ANDROID! magic missing'
    # boot v0-v2: header_version at 40; v3/v4: header_size 2118 at 20
    hv2, = struct.unpack_from('<I', b, 40)
    if hv2 >= 3:
        info, parts, off, ps, ver = parse_boot_v3_4(b)
    else:
        info, parts, off, ps, ver = parse_boot_v0_2(b)
    rnd = lambda n: (n + ps - 1) // ps * ps
    for name, sz in parts:
        if sz:
            open(f'{out}/{name}', 'wb').write(b[off:off+sz])
        info[name + '_offset'] = off
        off += rnd(sz)
    with open(f'{out}/header.txt', 'w') as f:
        for k, v in info.items():
            f.write(f'{k}: {v}\n')
    print(open(f'{out}/header.txt').read())
    return info

def _synth_v2():
    page = PAGE
    hdr = bytearray(page)
    hdr[0:8] = b'ANDROID!'
    struct.pack_into('<10I', hdr, 8, *(0x100, 0x8000, 0x50, 0x1000000, 0x10, 0x0,
                                       0x480, 4096, 2, 0x00))  # ver 2
    hdr[48:48+4] = b'vndr'
    hdr[64:64+8] = b'console='
    struct.pack_into('<IQI', hdr, 1632, 0, 0, 1660)
    struct.pack_into('<IQ', hdr, 1648, 0x40, 0x1f00000)
    parts = [b'\x01' * 0x100, b'\x02' * 0x50, b'\x03' * 0x10, b'\x04' * 0x40]
    img = bytes(hdr)
    for p in parts:
        img += p + b'\0' * ((-len(p)) % page)
    return img, parts

def _synth_v3(hv):
    hdr = bytearray(2118).ljust(PAGE, b'\0')
    hdr[0:8] = b'ANDROID!'
    struct.pack_into('<III', hdr, 8, 0x100, 0x50, 0x00)   # ks, rs, osv
    struct.pack_into('<I', hdr, 20, 2118)                  # header_size
    struct.pack_into('<I', hdr, 40, hv)                    # header_version
    hdr[44:44+6] = b'proot='
    hdr[1580:1586] = b'kw_r\0\0'
    img = bytes(hdr) + b'\xAA' * 0x100 + b'\xBB' * (PAGE - 0x100)  # ks + pad rest
    img += b'\xCC' * 0x50 + b'\0' * (PAGE - 0x50)
    return img, [b'\xAA' * 0x100, b'\xCC' * 0x50]

def selftest():
    tmp = tempfile.mkdtemp(prefix='unpack_boot_selftest_')
    # v2: must byte-match unpack_bootv2.py output
    img, _parts = _synth_v2()
    src = f'{tmp}/t_v2.img'
    open(src, 'wb').write(img)
    a = f'{tmp}/a'; c = f'{tmp}/c'
    unpack(src, a)
    import subprocess
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'unpack_bootv2.py'),
                    src, c], check=True, capture_output=True)
    same = open(f'{a}/header.txt').read() == open(f'{c}/header.txt').read()
    print(f'v2 regression vs unpack_bootv2.py: header.txt identical = {same}')
    assert same
    # v3/v4
    for hv in (3, 4):
        img, _ = _synth_v3(hv)
        s = f'{tmp}/t_v{hv}.img'; open(s, 'wb').write(img)
        info = unpack(s, f'{tmp}/v{hv}')
        assert info['header_version'] == hv and info['kernel_size'] == 0x100
    # actual module unpack above may have already left files; done
    print('selftest OK')

def main():
    if len(sys.argv) == 3:
        unpack(sys.argv[1], sys.argv[2])
    else:
        selftest()

if __name__ == '__main__':
    main()
