# Venus (Xiaomi 11, SM8350/lahaina) plan snip

Path: **venus via a white-mirror "probe" boot** — same shim technique as sweet
(sm7150), but for SM8350: `start.S` → `shim_main` → when the bootloader's FDT
`/chosen` has no `linux,initrd-start`, print a `VENUS PROBE` banner, CNTFRQ_EL0
and the PSCI version on the framebuffer, then hold on screen.

- Probe shim: `shim/venus_probe.c` (called from `shim_main` under
  `-DVENUS_PROBE`; the sweet/default build is untouched).
- SoC constants (FB address/resolution/stride, all TODO(dump) placeholders):
  `shim/soc_venus.h` — fill from the dump analysis before first `fastboot boot`.
- Build: `bash shim/build.sh --venus [3|4]` → `shim/out/shim-boot-venus.img`
  (boot header v3/v4; dtb belongs in vendor_boot/dtbo, see build.sh warning).
- Unpack: `tools/unpack_boot.py <image> <outdir>` (boot header v0-v4, plus
  vendor_boot v3/v4 fields in header.txt); `tools/unpack_bootv2.py` untouched.
- Boot partition facts (from dump work, confirm in report): venus boot = 192 MiB
  dual-slot, boot header v3/v4 + vendor_boot + dtbo.
