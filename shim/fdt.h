#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/* Read a NUL-terminated string property of an arbitrary node ("[/]a[/b…",
 * "@unit" suffix tolerated). Returns 0 if node/prop absent or the string
 * does not fit into out (len incl. NUL); out is NUL-terminated on success. */
int fdt_get_string(uintptr_t fdt, const char *path, const char *prop, char *out, int len);

/* Read a `reg` property of an arbitrary node as (base, size) u64 pairs.
 * Cell-count agnostic on the common layouts: 16 bytes (2+2 cells, 64-bit
 * address and size, the live venus /reserved-memory style) or 8 bytes
 * (1+1 cells, 32-bit). Returns 0 if node/reg absent or the encoding is
 * neither of those; on failure the outputs are untouched. */
int fdt_get_reg(uintptr_t fdt, const char *path, uint64_t *base, uint64_t *size);
