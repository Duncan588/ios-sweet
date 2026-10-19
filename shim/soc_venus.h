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

/* /reserved-memory/splash_region reg = 0xEA600000 size 0x2300000, label
 * "cont_splash_region" — ABL's framebuffers live here at boot. */
#define VENUS_FB_BASE		0xEA600000UL
#define VENUS_FB_REGION_SIZE	0x2300000UL	/* whole splash region          */

/* Two panel geometry options, from dtbo entry 46 ("xiaomi 38 08 0a"):
 * FHD  (default MI 11 setting): 1080x2400  -> stride 4320, fb 10,368,000 bytes
 * QHD+ (user setting):          1440x3200  -> stride 5760, fb 18,432,000 bytes
 * Both fit the 0x2300000 (36 MiB) splash region: expected framebuffer bytes
 * = W * H * 4 (a8r8g8b8) <= VENUS_FB_REGION_SIZE holds for either mode.
 * Default is FHD; build with -DVENUS_PANEL_QHD (build.sh "qhd" arg) for QHD. */
#define VENUS_FB_W_FHD		1080
#define VENUS_FB_H_FHD		2400
#define VENUS_FB_W_QHD		1440
#define VENUS_FB_H_QHD		3200
#ifndef VENUS_PANEL_QHD
#define VENUS_FB_W		1080	/* == VENUS_FB_W_FHD */
#define VENUS_FB_H		2400	/* == VENUS_FB_H_FHD */
#else
#define VENUS_FB_W		1440	/* == VENUS_FB_W_QHD */
#define VENUS_FB_H		3200	/* == VENUS_FB_H_QHD */
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

/* Probe banner string printed as "VENUS PROBE <string>". */
#define VENUS_PROBE_TAG		"venus-sm8350"
