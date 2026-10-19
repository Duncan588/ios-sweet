/*
 * Venus probe path v4 (Xiaomi 11, SM8350). Called from shim_main when built
 * with -DVENUS_PROBE; the sweet (sm7150) build is untouched. Entry logic:
 * start.S -> shim_main -> check FDT /chosen "linux,initrd-start"; when the
 * bootloader did not hand us an initrd, run the probe: FB base is read from
 * the ABL DTB (/reserved-memory/splash_region reg) at runtime, falling back
 * to soc_venus.h VENUS_FB_BASE when the node is absent; then the multi-base
 * banner sweep, CNTFRQ_EL0, SMCCC PSCI version, and a self-terminating
 * sequence: ~10 s busy-wait (CNTFRQ-scaled) -> PSCI SYSTEM_OFF; if the call
 * returns failure, PSCI SYSTEM_RESET.
 * v3 semantics: power off, reboot, or fall back to ABL — it never hangs on
 * screen. Returns 1 so shim_main does NOT fall through to the loader.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "soc_venus.h"
#include "venus_probe.h"

/* QHD is the live machine's panel setting (live Linux DTB: QHD 1440x3200 cmd
 * panel), so v4 forces it here instead of relying on a build flag. */
#ifndef VENUS_PANEL_QHD
#define VENUS_PANEL_QHD		1
#endif

#define PSCI_VERSION		0x84000000UL
#define PSCI_SYSTEM_OFF		0x84000008UL
#define PSCI_SYSTEM_RESET	0x84000009UL

/* Splash-region neighbors to try when the primary base does not light the
 * panel. fb_bases[0] is filled at entry from the ABL DTB
 * /reserved-memory/splash_region (the live-host value base 0xE2A00000 —
 * the static-dts 0xEA600000 was disproved); on a missing node it falls
 * back to VENUS_FB_BASE (0xE2A00000, soc_venus.h). */
static uintptr_t fb_bases[3];
#define FB_BASES_N	(sizeof(fb_bases) / sizeof(fb_bases[0]))
/* fb_bases[1]: +32 MiB into the region in case ABL moved the scanout;
 * fb_bases[2]: historical sm7150 cont_splash base. */

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static uint64_t venus_smc(uint64_t fn)
{
	register uint64_t x0 asm("x0") = fn;
	__asm__ volatile("smc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
	return x0;
}

static uint64_t venus_hvc(uint64_t fn)
{
	register uint64_t x0 asm("x0") = fn;
	__asm__ volatile("hvc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
	return x0;
}

/* Pick the PSCI conduit from the DTB /psci "method" property: QEMU virt has
 * no EL3 (TCG), where smc raises Undefined Instruction; real ABL/ATF venus
 * DTBs carry /psci too, so this is evidence-driven, not QEMU-only. */
static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uint64_t psci_call(uintptr_t dtb, uint64_t fn)
{
	char method[8] = {0};

	if (fdt_get_string(dtb, "/psci", "method", method, sizeof(method)) &&
	    str_eq(method, "hvc"))
		return venus_hvc(fn);
	if (!method[0])
		con_str("PSCI warn", "no /psci method in DTB, assuming smc");
	return venus_smc(fn);
}

/* psci_call_with_id: thin alias so call sites read as (dtb, fn-id); both
 * SYSTEM_OFF (0x84000008) and SYSTEM_RESET (0x84000009) take no arguments. */
#define psci_call_with_id(dtb, id)	psci_call((dtb), (id))

static void venus_delay_ten_seconds(void)
{
	uint64_t n = rsr("cntfrq_el0");	/* ticks per second */

	n *= 10;
	while (n--)
		__asm__ volatile("nop");
}

static void banner(uintptr_t base, const char *region_note)
{
	static char hex[] = "0123456789ABCDEF";
	char buf[9];
	int i;

	for (i = 0; i < 8; i++)
		buf[i] = hex[(base >> (4 * (7 - i))) & 0xf];
	buf[8] = 0;
	con_str("FB BASE", buf);			/* per-region marker line */
	if (region_note && region_note[0])
		con_str("REGION", region_note);
	con_str("VENUS", "PROBE v4 " VENUS_PROBE_TAG);
}

int venus_probe_entry(uintptr_t dtb)
{
	uint64_t initrd = 0;
	uint64_t off_ret;
	char method[8] = {0};

	if (fdt_chosen_u64(dtb, "linux,initrd-start", &initrd) && initrd)
		return 0;	/* initrd present: let the payload path handle it */

	/* FB base decision: read /reserved-memory/splash_region out of the
	 * ABL-provided DTB (the live Linux host proved base 0xE2A00000 —
	 * the static-dts 0xEA600000 caused three black screens). Missing
	 * node -> fall back to VENUS_FB_BASE (also 0xE2A00000 now). */
	uint64_t fb_base = VENUS_FB_BASE, fb_size = VENUS_FB_REGION_SIZE;
	const char *fb_src = "fallback: dtb splash_region absent";
	if (fdt_get_reg(dtb, "/reserved-memory/splash_region", &fb_base, &fb_size))
		fb_src = "dtb /reserved-memory/splash_region";
	fb_bases[0] = (uintptr_t)fb_base;
	fb_bases[1] = (uintptr_t)fb_base + 0x02000000UL;	/* ABL may have moved scanout */
	fb_bases[2] = 0x9C000000UL;			/* sm7150-era cont_splash */

	/* Region 0: full banner on a cleared screen. */
	con_fb = (volatile uint32_t *)fb_bases[0];
	con_clear(BLUE);
	banner(fb_bases[0], "primary (dt splash_region)");
	con_hex2("FB", "base", (uint32_t)fb_base, "size", (uint32_t)fb_size);
	con_str("FB-SRC", fb_src);
	con_str("INITRD", "absent in /chosen (vendor_boot ramdisk not wired)");
	con_hex("CNTFRQ_EL0", rsr("cntfrq_el0"));
	fdt_get_string(dtb, "/psci", "method", method, sizeof(method));
	con_str("PSCI method", method[0] ? method : "absent in DTB (assuming smc)");
	con_hex("PSCI ver", psci_call(dtb, PSCI_VERSION));

	/* Regions 1..n: same-size banner re-draw, no clear, base value shown. */
	for (unsigned i = 1; i < FB_BASES_N; i++) {
		con_fb = (volatile uint32_t *)fb_bases[i];
		con_reset_row();
		banner(fb_bases[i], "alt candidate");
	}

	/* Back to the documented base for the closing line, then the
	 * self-terminating sequence burned in before the off/reset attempt. */
	con_fb = (volatile uint32_t *)fb_bases[0];
	con_skip();
	con_str("VENUS", "probe v4 done, system_off next");
	venus_delay_ten_seconds();

	off_ret = psci_call_with_id(dtb, PSCI_SYSTEM_OFF);
	if (off_ret != 0) {			/* PSCI NOT_SUPPORTED etc. */
		con_fb = (volatile uint32_t *)fb_bases[0];
		con_skip();
		con_str("SYSTEM_OFF", "rejected, system_reset next");
		psci_call_with_id(dtb, PSCI_SYSTEM_RESET);
	}
	return 1;	/* unreachable on success; keeps -Wreturn-type quiet and
			 * tells shim_main the probe path fully took over */
}
