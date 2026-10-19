#pragma once
#include <stdint.h>

/* Returns 1 if probe mode took over (does not return), 0 to continue the
 * sweet loader path in shim_main. Guarded by -DVENUS_PROBE at the caller. */
int venus_probe_entry(uintptr_t dtb);

/* venus CPU bring-up hooks (P-agent port, implemented in shim.c under
 * -DVENUS_PROBE). Called from the probe path only, after the PSCI banner:
 *   venus_cpu_ident        reads/prints MIDR + MPIDR + Aff1 family (no MMIO)
 *   venus_gicr_prepare_cpus  wakes all 8 redistributors + GICR IGROUPR0/
 *                            ISENABLER0 PPI (vtimer 27/ptimer 30/SGI1),
 *                            topaz 66ae4bf+ f26e7a5 semantics minus the
 *                            venus-aborting IPRIORITYR writes (TODO)
 *   venus_speed_probe      boot-core dependent-add tick probe (no cpufreq
 *                            MMIO; topaz gold_cpufreq_max is not ported) */
void venus_cpu_ident(void);
void venus_gicr_prepare_cpus(void);
void venus_speed_probe(void);
