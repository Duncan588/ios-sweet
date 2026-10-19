#!/bin/bash
# Venus (Xiaomi Mi 11, M2011K2C) real-device `fastboot boot` probe.
#   tools/venus_boot_probe.sh <bootimage>
# Run on the NOTEBOOK (the machine that has adb/fastboot and the phone on USB);
# the Windows side of E:\Hackios cannot see the device.
#
# `fastboot boot` is a TEMPORARY boot: the image is booted from RAM, nothing
# is written to any partition (boot_b stays untouched). BL is unlocked
# (verifiedbootstate=orange), so it is allowed. See tools/venus_recover_note.md
# for the black-screen recovery drill.
#
# Safety: this script contains NO flash/erase/format/oem commands. It never
# reboots the device on its own after booting the image — the watch window
# (screen, camera, serial) is kept for human eyes. adb reboot bootloader is
# the only reboot, and only when the device is not already in fastboot.
set -euo pipefail

if [ $# -ne 1 ]; then
	echo "usage: $0 <bootimage>   e.g. shim/out/shim-boot.img" >&2
	exit 2
fi
img=$1
if [ ! -f "$img" ]; then
	echo "ERROR: image not found: $img" >&2
	exit 2
fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ts=$(date +%Y%m%d-%H%M%S)
logdir=$repo/logs
logfile=$logdir/venus-probe-$ts.log
mkdir -p "$logdir"
# everything (stdout+stderr) of the whole run is also captured
exec > >(tee -a "$logfile") 2> >(tee -a "$logfile" >&2)

echo "== venus boot probe $(date +%F\ %T), image=$img =="

have_state() { fastboot devices 2>/dev/null | grep -q .; }
have_adb()   { adb devices 2>/dev/null | awk 'NR>1{print $2}' | grep -qx device; }

if have_state; then
	echo "[pre] device already in fastboot mode"
elif have_adb; then
	echo "[pre] adb device online; rebooting to bootloader"
	adb reboot bootloader
	i=0
	until have_state; do
		i=$((i+1))
		if [ "$i" -gt 40 ]; then
			echo "ERROR: no fastboot device after 40s" >&2
			exit 1
		fi
		sleep 1
	done
	echo "[pre] fastboot device appeared after ${i}s"
else
	echo "ERROR: neither adb device nor fastboot device detected" >&2
	exit 1
fi

# --- facts, one line each; values are printed, not interpreted ---
echo "[getvar] unlocked:    $(fastboot getvar unlocked 2>&1 | head -1)"
echo "[getvar] current-slot: $(fastboot getvar current-slot 2>&1 | head -1)"
echo "[getvar] max-download-size: $(fastboot getvar max-download-size 2>&1 | head -1)"

# --- the probe itself ---
echo "[boot] START $(date +%F\ %T)"
if fastboot boot "$img"; then
	echo "[boot] fastboot boot returned ok END $(date +%F\ %T)"
else
	echo "[boot] fastboot boot returned non-zero END $(date +%F\ %T)" >&2
fi

echo "=== OBSERVATION WINDOW STARTS $(date +%F\ %T) ==="
echo "Device NOT rebooted by script. Record screen/camera/serial by hand."
echo "Log saved: $logfile"
