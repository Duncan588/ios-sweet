#!/usr/bin/env python3
"""Minimal `llvm-nm --defined-only` replacement: prints "ADDRESS SYMBOLNAME" for
every defined (non-zero-value) LC_SYMTAB symbol of a Mach-O 64 little-endian file."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, res = struct.unpack_from('<8I', d, 0)
assert magic == 0xFEEDFACF, f'not a Mach-O 64 LE file: {magic:#x}'
off = 32
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd == 0x02:  # LC_SYMTAB
        symoff, nsyms, stroff, strsize = struct.unpack_from('<4I', d, off + 8)
        strtab = d[stroff:stroff + strsize]
        for i in range(nsyms):
            o = symoff + i * 16
            nstrx, ntype, nsect, ndesc, value = struct.unpack_from('<IBBHQ', d, o)
            if value == 0:
                continue
            end = strtab.index(b'\0', nstrx)
            print(f'{value:016x} {strtab[nstrx:end].decode("ascii", "replace")}')
        sys.exit(0)
    off += size
sys.stderr.write('no LC_SYMTAB found\n')
sys.exit(1)
