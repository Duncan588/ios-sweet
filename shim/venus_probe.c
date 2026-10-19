/*
 * Venus probe path (Xiaomi 11, SM8350). Called from shim_main when built
 * with -DVENUS_PROBE; the sweet (sm7150) build is untouched. Entry logic:
 * start.S -> shim_main -> check FDT /chosen "linux,initrd-start"; when the
 * bootloader did not hand us an initrd, run the probe screen: banner,
 * CNTFRQ_EL0 and the SMCCC PSCI version. Returns 0 to fall through to the
 * sweet loader path (initrd present), never returns inside probe mode.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "soc_venus.h"
#include "venus_probe.h"

#define PSCI_VERSION		0x84000000UL
#define PSCI_SYSTEM_RESET	0x84000009UL

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

static void venus_spin(void)
{
	for (;;)
		__asm__ volatile("wfe");
}

int venus_probe_entry(uintptr_t dtb)
{
	uint64_t initrd = 0;

	if (fdt_chosen_u64(dtb, "linux,initrd-start", &initrd) && initrd)
		return 0;	/* initrd present: let the payload path handle it */

	con_clear(BLUE);
	con_skip();
	con_str("VENUS", "PROBE " VENUS_PROBE_TAG);	/* VENUS PROBE <string> */
	if (initrd)	/* initrd marker present but zero: report as such */
		con_hex("INITRD", initrd);
	else
		con_str("INITRD", "absent in /chosen (vendor_boot ramdisk not wired)");
	con_hex("CNTFRQ_EL0", rsr("cntfrq_el0"));
	{
		char method[8] = {0};
		fdt_get_string(dtb, "/psci", "method", method, sizeof(method));
		con_str("PSCI method", method[0] ? method : "absent in DTB (assuming smc)");
		con_hex("PSCI ver", psci_call(dtb, PSCI_VERSION));
	}
	con_skip();
	con_str("VENUS", "probe done, no reset call (held on screen)");
	venus_spin();
}
