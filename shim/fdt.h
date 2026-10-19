#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/* Read a NUL-terminated string property of an arbitrary node ("[/]a[/b…",
 * "@unit" suffix tolerated). Returns 0 if node/prop absent or the string
 * does not fit into out (len incl. NUL); out is NUL-terminated on success. */
int fdt_get_string(uintptr_t fdt, const char *path, const char *prop, char *out, int len);
