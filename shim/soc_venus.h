#pragma once
/*
 * Xiaomi 11 (venus, SM8350/lahaina) framebuffer + SoC constants for the
 * venus probe shim.
 *
 * All values below are read from the on-device dumps (venus-probe, 2026-10-19):
 *   boot_b          98CB9CCA36AA49C18100A9FEE369E8FC4294CD29EB3E44DE3C93AFEBAA8839B4
 *   vendor_boot_b   F0999813278EC3DB24A7DBA13597E599399B50CB1AA5A36F2B929D63FE8FAE2A
 *   dtbo_b          EEBAFBA086EF48318B7500B95515EDC27CF57E129E8E64E3671C42CE93947018
 * Sources: vendor_boot SoC DTB "Lahaina SoC" (reserved-memory splash_region)
 * and dtbo entry 46 ("Venus based on SM8350", dsi panel "xiaomi 38 08 0a").
 * Full evidence: docs/RESEARCH_LOG.md (2026-10-19 entry) and
 * E:\Hackios\Xiaomi11\out\{dtb_nodes.txt,dtbo_entry46_venus.dts}.
 */
#include <stdint.h>

/* /reserved-memory/splash_region reg. REPLACED 2026-10-19 by the live-host
 * value: Linux runtime DT (fdt_live.bin, dtb_idx=4/dtbo_idx=46) shows
 * base 0xE2A00000 size 0x2300000 — the static vendor_boot SoC DTB value
 * 0xEA600000 ("cont_splash_region") is disproved as the live framebuffer
 * location and was the root cause of three consecutive black screens.
 * On real boot ABL hands the shim its own DTB in x0; shim/venus_probe.c
 * reads this path from it at runtime and only falls back to this constant
 * when /reserved-memory/splash_region is missing. */
#define VENUS_FB_BASE		0xE2A00000UL	/* live-host splash_region reg */
#define VENUS_FB_REGION_SIZE	0x2300000UL	/* whole splash region          */

/* Label "cont_splash_region": ABL's framebuffers used to live here at boot
 * (static vendor_boot DTB) — kept as documentation, no longer the FB base. */
#define VENUS_FB_BASE_STATIC_DTS	0xEA600000UL

/* Two panel geometry options, from dtbo entry 46 ("xiaomi 38 08 0a"):
 * FHD  (default MI 11 setting): 1080x2400  -> stride 4320, fb 10,368,000 bytes
 * QHD+ (user setting):          1440x3200  -> stride 5760, fb 18,432,000 bytes
 * Both fit the 0x2300000 (36 MiB) splash region: expected framebuffer bytes
 * = W * H * 4 (a8r8g8b8) <= VENUS_FB_REGION_SIZE holds for either mode.
 * Default is QHD (the live machine's actual user setting; the live Linux
 * DTB reports the QHD 1440x3200 cmd panel): chrome default below, so plain
 * `bash build.sh --venus` builds QHD and the qemu-venus.py fb parser sees
 * the same first-match values. FHD is a fallback option and needs an
 * explicit build flag / file edit. */
/* v4 default: QHD+ — the live machine's actual user setting (Linux DT,
 * dtb_idx=4/dtbo_idx=46, reports the QHD 1440x3200 cmd panel). Applies to
 * every TU that includes this header (console drawing included). FHD is a
 * fallback option: build with -DVENUS_PANEL_FHD (needs an explicit build
 * flag hack to undefine the default, or edit this file). */
#ifndef VENUS_PANEL_QHD
#define VENUS_PANEL_QHD		1
#endif

#define VENUS_FB_W_FHD		1080
#define VENUS_FB_H_FHD		2400
#define VENUS_FB_W_QHD		1440
#define VENUS_FB_H_QHD		3200
#ifdef VENUS_PANEL_QHD
#define VENUS_FB_W		1440	/* == VENUS_FB_W_QHD (literal: host fb parsers regex-match digits) */
#define VENUS_FB_H		3200	/* == VENUS_FB_H_QHD */
#else
#define VENUS_FB_W		1080	/* == VENUS_FB_W_FHD */
#define VENUS_FB_H		2400	/* == VENUS_FB_H_FHD */
#endif
#define VENUS_FB_STRIDE		(VENUS_FB_W * 4) /* a8r8g8b8 assumption; bpp   */
				/* NOT in DT — panel bpp=24 dsi; DPU fb format  */
				/* read at runtime if first draw looks wrong.  */

/* arm,armv8-timer (SoC DTB, /timer) interrupts in DT order:
 *  <1 13> secure physical timer  -> INTID 29
 *  <1 14> non-secure physical    -> INTID 30   (cntp at EL0/EL1 ns)
 *  <1 11> virtual timer          -> INTID 27   (cntv)  <== use as vtimer
 *  <1 12> hypervisor timer       -> INTID 28
 * NOTE shim.c still hardcodes 19/18 (732G values); wire these in.  */
#define VENUS_VTIMER_INTID	27
#define VENUS_NPTIMER_INTID	30	/* non-secure physical (cntp_ctl_el0) */
#define VENUS_HTIMER_INTID	28
#define VENUS_STIMER_INTID	29

/* GIC (SoC DTB /soc/interrupt-controller@17a00000) — matches shim.c:
 * reg = <0x17A00000 0x10000  0x17A60000 0x100000>; #redistributor-regions=1,
 * redistributor-stride = 0x20000 (per-PE). No shim.c change needed. */

/* ARM arch timer counter frequency (DT clock-frequency): 0x124F800 = 19.2 MHz.
 * MDP: ufshc@1d84000 reg = 0x1d84000/0x3000 (ufs_mem), 0x1d88000/0x8000 (ice),
 * 0x1d90000/0x9000 (ice_hwkm); IRQ SPI+0x109.  */

/* ---- CPU bring-up constants (P-agent port from topaz 66ae4bf/f26e7a5) ----
 * SM8350 layout (assets/NOTES, 2026-10-19): 4x silver cluster (Aff1=0),
 * 3x gold (Aff1=1, Aff0=1..3 - gold0 Aff0=0 is the base core, no MPIDR 0x100),
 * 1x prime (Aff1=2, Aff0=0). Boot CPU from ABL: silver0 MPIDR 0x0.
 * mmio offsets per frame: GICR stride 0x20000 (shim.c GICR_PE_SIZE). */
#define VENUS_NR_CPUS		8
static const uint32_t venus_mpidrs[VENUS_NR_CPUS] = {
	0x000, 0x001, 0x002, 0x003,		/* silver 0-3 */
	0x101, 0x102, 0x103,			/* gold 1-3 (gold0 == 0x101? NO: see below) */
	0x200					/* prime */
};
/* Correction note: Aff1=1 Aff0=1..3 -> MPIDR 0x101..0x103. MPIDR Aff0 field is
 * bits 7:0, so 0x101 = Aff1=1, Aff0=1. Gold0 would be 0x100 but SM8350 gold
 * cluster only has core0 as the base core at 0x100? Left at the documented
 * venus truth: no 0x100 redistributor frame exists (3 gold cores total). */

/* TODO(P-agent, speed probe): topaz (SM7150) gold cpufreq-hw freq-domain1 is
 * 0x0f523000 (LUT +0x110, PERF_STATE +0x920). No verified SM8350 equivalent
 * base is in the asset set, so NO cpufreq MMIO is done in the venus shim -
 * only the tick-counting probe framework (venus_speed_probe in shim.c). */

/* Probe banner string printed as "VENUS PROBE <string>". */
#define VENUS_PROBE_TAG		"venus-sm8350"
