#!/usr/bin/env python3
"""QEMU smoke test for the venus (SM8350) probe shim (venus-probe branch).

  qemu-venus.py SHIM-IMAGE OUTDIR [--secs N] [--gdb] [--fail-on-panic]
  qemu-venus.py --selftest          (no QEMU needed: renders/decodes the fb)

Minimal adaptation of tools/qemu-sweet.py. Instead of unpacking a SWEETXNU
xnu payload, it loads shim/out/Image (the build.sh --venus raw product, or a
matching ELF via --image-format elf) at a fixed RAM address, hands it a DTB
in x0 like ABL would, and lets the vendored QEMU virt DTB (no /chosen
initrd) steer shim_main into the VENUS_PROBE path (venus_probe.c). Success
criterion: the framebuffer decodes a line containing "VENUS PROBE".

fb geometry comes from shim/soc_venus.h (0x9d000000/1080x2400, placeholders
until the dump report); the function screen lies inside QEMU virt RAM
(0x40000000.. with -m 4G), so it is plain memory like on qemu-sweet.py.

QEMU virt models neither SM8350 XPU/bootloader/panel nor the SMC PSCI
conduit the phone uses: this is a code-logic regression, not hardware
fidelity. No vendor_boot is emulated; the DTB is QEMU's own (or --dtb).

Exit codes with --fail-on-panic: 2 = visible panic in the decoded screen,
3 = decode failed/blank or no "VENUS PROBE", 0 = banner found.
Without it: 1 when QEMU exits early, 0 otherwise.
"""
import argparse, json, os, re, socket, struct, subprocess, sys, time, zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOC_H = os.path.join(REPO, 'shim', 'soc_venus.h')
FONT_H = os.path.join(REPO, 'shim', 'font.h')
PAGE = 0x4000
IMG_PA = 0x42000000		# shim copy (linker has no absolute relocs but keep CBFS-style placement above the QEMU DTB)
STUB_PA = 0x40200000	# above the dtb QEMU places at RAM base (0x40000000)
PROBE_BANNER_RE = r'\bVENUS\s+PROBE\b'	# con_str("VENUS", "PROBE ...") leaves a col gap


def soc_consts(path=SOC_H):
	"""Parse the fb constants out of soc_venus.h (single source of truth)."""
	src = open(path, encoding='utf-8').read()

	def name(n):
		m = re.search(rf'^#define\s+{n}\s+([^\s/]+)', src, re.M)
		if not m:
			raise ValueError(f'{n} not found in {path}')
		return m.group(1)

	w = int(name('VENUS_FB_W'))
	h = int(name('VENUS_FB_H'))
	base = int(name('VENUS_FB_BASE').rstrip('UL'), 0)
	stride_d = re.search(r'^#define\s+VENUS_FB_STRIDE\s+([^(]*\([^)]*\))', src, re.M).group(1).strip()
	stride = w * 4	# soc_venus.h must keep VENUS_FB_STRIDE == VENUS_FB_W * 4 (a8r8g8b8)
	assert re.fullmatch(r'\(\s*VENUS_FB_W\s*\*\s*4\s*\)', stride_d), f'unexpected VENUS_FB_STRIDE {stride_d!r}'
	return {'fb_base': base, 'fb_w': w, 'fb_h': h, 'fb_stride': stride}


def load_font(path=FONT_H):
	"""font.h: `static const unsigned char font8x16[95][16] = {...}` of hex bytes."""
	src = open(path, encoding='utf-8').read()
	chars = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{2}', src)]
	if len(chars) != 95 * 16:
		raise ValueError(f'{path}: expected 95*16 font bytes, got {len(chars)}')
	chars = bytes(chars)
	return {chars[c * 16:(c + 1) * 16]: chr(0x20 + i) for i, c in enumerate(range(95))}


def build_stub(dtb_pa, entry_pa):
	# ldr x0, =dtb ; ldr x1, =entry ; br x1 ; nop ; .quad dtb ; .quad entry
	return struct.pack('<4I2Q', 0x58000080, 0x580000a1, 0xd61f0020, 0xd503201f, dtb_pa, entry_pa)


def load_image(raw, fmt):
	"""Return (image_bytes_at_link, entry_offset_into_it). bin: Image = shim.ld layout (entry at file offset 0)."""
	if fmt == 'bin':
		return raw, 0
	# Minimal ELF64: concatenate PT_LOAD segments in vaddr order, entry = e_entry - lowest vaddr.
	if raw[:4] != b'\x7fELF' or raw[4] != 2 or raw[5] != 1:
		raise ValueError('not a little-endian ELF64 image')
	e_entry, = struct.unpack_from('<Q', raw, 0x18)
	phoff, phentsize, phnum = struct.unpack_from('<Q', raw, 0x20)[0], *struct.unpack_from('<HH', raw, 0x36)
	loads, lo = [], None
	for i in range(phnum):
		p = phoff + i * phentsize
		p_type, p_flags, p_off, p_vaddr, _, p_filesz = struct.unpack_from('<IIQQQQ', raw, p)
		if p_type == 1:	# PT_LOAD
			loads.append((p_vaddr, raw[p_off:p_off + p_filesz]))
			lo = p_vaddr if lo is None else min(lo, p_vaddr)
	if not loads:
		raise ValueError('ELF has no PT_LOAD segments')
	loads.sort()
	out = bytearray()
	for vaddr, data in loads:
		out += b'\0' * (vaddr - lo - len(out))
		out += data
	return bytes(out), e_entry - lo


def write_png(path, raw, w, h, stride):
	rows = []
	for y in range(h):
		line = raw[y * stride:(y + 1) * stride]
		rgb = bytearray(w * 3)	# a8r8g8b8 little endian = B G R A in memory
		rgb[0::3], rgb[1::3], rgb[2::3] = line[2::4], line[1::4], line[0::4]
		rows.append(b'\0' + bytes(rgb))
	chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
	png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
	png += chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
	open(path, 'wb').write(png)


def decode_screen(raw, fb, font):
	"""Exact cell decoding of shim/console.c: SCALE=3, 24x48 cells, 24px margin.

	A pixel is "on" when it differs from the background color (read from the
	fb corner), which covers both YELLOW labels and WHITE text on BLUE.
	"""
	w, h, stride, scale = fb['fb_w'], fb['fb_h'], fb['fb_stride'], 3
	ch_w, ch_h = 8 * scale, 16 * scale
	margin = 24
	px = lambda x, y: struct.unpack_from('<I', raw, y * stride + x * 4)[0]
	bg = px(0, 0)
	rows, unknown = [], 0
	for row in range((h - 2 * margin) // ch_h):
		chars = []
		for col in range((w - 2 * margin) // ch_w):
			Glyph = bytearray(16)
			for gy in range(16):
				for gx in range(8):
					if px(margin + col * ch_w + gx * scale + scale // 2,
					      margin + row * ch_h + gy * scale + scale // 2) != bg:
						Glyph[gy] |= 0x80 >> gx
			key = bytes(Glyph)
			char = font.get(key, '?')
			known = key in font
			unknown += not known
			chars.append(char)
		rows.append(''.join(chars).rstrip())
	screen = '\n'.join(rows).rstrip() + '\n'
	return screen, unknown, bg


def render_screen(text_lines, fb, font):
	"""Host-side replica of console.c's blitter (selftest only)."""
	w, h, stride, scale = fb['fb_w'], fb['fb_h'], fb['fb_stride'], 3
	ch_w, ch_h = 8 * scale, 16 * scale
	bg, fg, yellow = 0xff000080, 0xffffffff, 0xff00ffff	# BLUE, WHITE, YELLOW (a8r8g8b8)
	bg_row = struct.pack('<I', bg) * w
	raw = bytearray()
	raw += bg_row * h
	struct.pack_into('<I', raw, 0, bg)	# corner = bg reference for decode_screen

	def put_char(col, row, ch, color):
		ch = ord(ch)
		if ch < 0x20 or ch > 0x7e:
			ch = 0x3f	# console.c maps non-printable to '?'
		g = font_glyph(font, ch)
		x0, y0 = 24 + col * ch_w, 24 + row * ch_h
		for gy in range(16):
			for gx in range(8):
				c = color if (g[gy] & (0x80 >> gx)) else bg
				for sy in range(scale):
					base = (y0 + gy * scale + sy) * stride + (x0 + gx * scale) * 4
					for sx in range(scale):
						struct.pack_into('<I', raw, base + sx * 4, c)

	for r, (label, body) in enumerate(text_lines):
		for i, c in enumerate(label):
			put_char(i, r, c, yellow)
		for i, c in enumerate(body):
			put_char(8 + i, r, c, fg)
	return bytes(raw)


def font_glyph(font, ch):
	for g, c in font.items():
		if ord(c) == ch:
			return g
	raise KeyError(ch)


def screen_status(screen, unknown):
	if re.search(PROBE_BANNER_RE, screen):
		status = 'venus_probe_ok'
	elif 'panic(cpu ' in screen or 'SWEET EARLY PANIC:' in screen:
		status = 'panic'
	elif unknown:
		status = 'incomplete_decode'
	elif screen.strip():
		status = 'no_banner'
	else:
		status = 'blank_screen'
	return {'status': status, 'unknown_glyphs': unknown, 'snapshot_only': True}


def qmp(sock_path, *cmds, pmemsave=None):
	s = socket.socket(socket.AF_UNIX)
	s.connect(sock_path)
	f = s.makefile('rw')
	f.readline()

	def call(execute, **args):
		f.write(json.dumps({'execute': execute, 'arguments': args}) + '\n')
		f.flush()
		while True:
			r = json.loads(f.readline())
			if 'return' in r or 'error' in r:
				return r

	call('qmp_capabilities')
	out = []
	for c in cmds:
		r = call('human-monitor-command', **{'command-line': c})
		if 'error' in r:
			print(f'qmp {c!r}: {r["error"]}', file=sys.stderr)
		out.append(r.get('return', ''))
	if pmemsave:	# native QMP: HMP's argument parser chokes on '-' in paths
		r = call('pmemsave', val=pmemsave[0], size=pmemsave[1], filename=pmemsave[2])
		if 'error' in r:
			print(f'qmp pmemsave: {r["error"]}', file=sys.stderr)
	s.close()
	return out


def selftest(fb, font):
	"""Render the expected probe screen with console.c's blitter, decode it."""
	lines = [('VENUS', 'PROBE venus-sm8350'),
		 ('INITRD', 'absent in /chosen (vendor_boot ramdisk not wired)'),
		 ('CNTFRQ_', '00170000')]
	raw = render_screen(lines, fb, font)
	screen, unknown, _ = decode_screen(raw, fb, font)
	assert unknown == 0, f'selftest: {unknown} unknown glyphs'
	assert re.search(PROBE_BANNER_RE, screen), 'selftest: banner not found\n' + screen
	assert 'venus-sm8350' in screen, 'selftest: tag not found\n' + screen
	# '?' fallback and non-ASCII map to '?'
	raw2 = render_screen([('XYZ', '\x7f\x01A'), ], fb, font)
	screen2, unknown2, _ = decode_screen(raw2, fb, font)
	assert screen2.rstrip().splitlines()[0].endswith('XYZ     ??A'), repr(screen2)
	print('selftest OK: blitter/decoder roundtrip with font.h glyphs, banner tag found')
	return 0


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument('image', nargs='?')
	ap.add_argument('outdir', nargs='?')
	ap.add_argument('--secs', type=float, default=20)
	ap.add_argument('--gdb', action='store_true', help='start halted, gdb stub on :1234, no timeout')
	ap.add_argument('--hmp', action='append', default=[], help='extra monitor command run after stop (repeatable)')
	ap.add_argument('--cpu', default='cortex-a76')	# SM8350 is X1/A78; virt needs >= ARMv8 compat for the probe only
	ap.add_argument('--image-format', choices=('bin', 'elf'), default='bin')
	ap.add_argument('--dtb', help='DTB file to supply in x0 instead of QEMU virt DTB (no guest initrd either way)')
	ap.add_argument('--fail-on-panic', action='store_true',
			help='exit 2 for a visible panic, 3 for blank/unknown/no-banner decoding')
	ap.add_argument('--selftest', action='store_true', help='host blitter/decoder roundtrip, no QEMU run')
	a = ap.parse_args()

	fb = soc_consts()
	font = load_font()
	if a.selftest:
		return selftest(fb, font)
	if not a.image or not a.outdir:
		ap.error('image and outdir are required (or use --selftest)')
	img, entry_off = load_image(open(a.image, 'rb').read(), a.image_format)
	dtbs = []
	if a.dtb:
		dtbs.append(open(a.dtb, 'rb').read())
	os.makedirs(a.outdir, exist_ok=True)
	out = os.path.abspath(a.outdir)

	entry_pa = IMG_PA + entry_off
	open(f'{out}/shim.bin', 'wb').write(img)
	open(f'{out}/stub.bin', 'wb').write(build_stub(0x40000000, entry_pa))
	print(f'shim at {IMG_PA:#x} entry={entry_pa:#x} dtb=0x40000000 (QEMU virt) '
	      f'fb={fb["fb_base"]:#x} {fb["fb_w"]}x{fb["fb_h"]} stride={fb["fb_stride"]}', file=sys.stderr)

	mon = f'{out}/mon.sock'
	if os.path.exists(mon):
		os.unlink(mon)
	# virt RAM starts at 0x40000000; 4G covers the soc_venus.h framebuffer and the shim copy.
	cmd = ['qemu-system-aarch64', '-M', 'virt,gic-version=3', '-cpu', a.cpu, '-smp', '1', '-m', '4G',
	       '-nographic', '-serial', f'file:{out}/serial.txt', '-qmp', f'unix:{mon},server,nowait',
	       '-d', 'int,guest_errors,unimp', '-D', f'{out}/qemu.log', '-no-reboot',
	       '-device', f'loader,file={out}/shim.bin,addr={IMG_PA:#x},force-raw=on',
	       '-device', f'loader,file={out}/stub.bin,addr={STUB_PA:#x},force-raw=on',
	       '-device', f'loader,addr={STUB_PA:#x},cpu-num=0']
	for d in dtbs:
		open(f'{out}/dtb.bin', 'wb').write(d)
		cmd += ['-device', f'loader,file={out}/dtb.bin,addr=0x40000000,force-raw=on']
	if a.gdb:
		cmd += ['-S', '-gdb', 'tcp::1234']
	p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL)
	if a.gdb:
		print('gdb stub on :1234; QMP socket:', mon, file=sys.stderr)
		p.wait()
		return
	t0 = time.time()
	while p.poll() is None and time.time() - t0 < a.secs:
		time.sleep(0.2)
	result = {'status': 'qemu_exited', 'qemu_returncode': p.returncode}
	if p.poll() is None:
		_, regs, *extra = qmp(mon, 'stop', 'info registers', *a.hmp,
				      pmemsave=(fb['fb_base'], fb['fb_stride'] * fb['fb_h'], f'{out}/fb.raw'))
		open(f'{out}/regs.txt', 'w').write(regs + ''.join(f'\n(qemu) {c}\n{r}' for c, r in zip(a.hmp, extra)))
		p.kill()
		p.wait()
		raw = open(f'{out}/fb.raw', 'rb').read()
		if len(raw) == fb['fb_stride'] * fb['fb_h']:
			write_png(f'{out}/fb.png', raw, fb['fb_w'], fb['fb_h'], fb['fb_stride'])
			screen, unknown, bg = decode_screen(raw, fb, font)
			open(f'{out}/fb.txt', 'w').write(screen)
			result = screen_status(screen, unknown)
			result['bg_color'] = f'{bg:#010x}'
		else:
			result = {'status': 'incomplete_decode', 'note': f'fb dump {len(raw)} bytes'}
		os.unlink(f'{out}/fb.raw')
	else:
		print(f'qemu exited early: {p.returncode}', file=sys.stderr)
	open(f'{out}/result.json', 'w').write(json.dumps(result, indent=2) + '\n')
	print(json.dumps(result), file=sys.stderr)
	print(f'{out}/fb.png  {out}/fb.txt  {out}/regs.txt  {out}/qemu.log', file=sys.stderr)
	if a.fail_on_panic and result['status'] == 'panic':
		return 2
	if a.fail_on_panic and result['status'] != 'venus_probe_ok':
		return 3
	if result['status'] == 'qemu_exited':
		return 1
	return 0


if __name__ == '__main__':
	sys.exit(main())
