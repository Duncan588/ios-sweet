#pragma once
#include <stdint.h>

/* Returns 1 if probe mode took over (does not return), 0 to continue the
 * sweet loader path in shim_main. Guarded by -DVENUS_PROBE at the caller. */
int venus_probe_entry(uintptr_t dtb);
