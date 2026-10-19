#!/bin/bash
# Build the shim and wrap it in a boot.img v2 that mirrors the stock PixelOS header.
#   ./build.sh [payload]              default sweet (sm7150/sweet) path, unchanged
#   ./build.sh --venus [3|4] [payload] venus (SM8350) probe image, boot header v3/v4
set -euo pipefail
cd "$(dirname "$0")"
CC=aarch64-linux-gnu-gcc
OBJCOPY=aarch64-linux-gnu-objcopy
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
mkdir -p out

VENUS=0
if [ "${1:-}" = "--venus" ]; then
	shift
	VENUS=1
	VENUS_HV=3
	case "${1:-}" in
		3|4) VENUS_HV=$1; shift ;;
	esac
	echo "warning: venus mode: header_version=$VENUS_HV, FB/etc values in soc_venus.h are TODO(dump) placeholders"
	if [ -n "${1:-}" ]; then echo "warning: venus v$VENUS_HV boot header carries no dtb slot; '$1' will NOT be packed" \
		"(dtb belongs in vendor_boot/dtbo; run dump packing separately)"; fi
fi

for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c; do
	$CC $CFLAGS $([ "$VENUS" = 1 ] && echo -DVENUS_PROBE) -c $f -o out/${f%.*}.o
done
# venus probe object only in --venus mode: sweet builds stay byte-identical.
if [ "$VENUS" = 1 ]; then
	$CC $CFLAGS -DVENUS_PROBE -c venus_probe.c -o out/venus_probe.o
fi
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$(aarch64-linux-gnu-readelf -r out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o \
	$([ "$VENUS" = 1 ] && echo out/venus_probe.o) -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio (ABL dislikes a zero-size ramdisk on some builds) -> diagnostics.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
fi
if [ "$VENUS" = 1 ]; then
	# venus: v3/v4 boot header (no page/base/offset/ddt slots in the v3 boot part).
	mkbootimg --header_version "$VENUS_HV" --kernel out/Image --ramdisk out/ramdisk.bin \
		--os_version 17.0.0 --os_patch_level 2035-06 \
		--cmdline "console=null" -o out/shim-boot-venus.img
	ls -l out/Image out/shim-boot-venus.img
	exit 0
fi
mkbootimg --header_version 2 --kernel out/Image --ramdisk out/ramdisk.bin \
	--dtb ../dtb/boot_unpacked/dtb --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
	--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
	--os_version 17.0.0 --os_patch_level 2035-06 \
	--cmdline "console=null" -o out/shim-boot.img
# Host render of the console layout.
gcc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test
ls -l out/Image out/shim-boot.img
