/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

/* Match one path component against a node name, tolerating a "@unit" suffix
 * (e.g. component "psci" matches node "psci" and "psci@4b00000"). */
static int node_match(const char *name, const char *comp, int comp_len)
{
	const char *n = name;
	while (*n && *n == *comp && comp_len) {
		n++, comp++, comp_len--;
	}
	return !comp_len && (*n == '\0' || *n == '@');
}

/* Path-walk shared by fdt_get_string/fdt_get_reg: locate the property
 * `prop` inside the node at `path` and hand back its raw value and length. */
static int fdt_walk_prop(uintptr_t fdt, const char *path, const char *prop,
			 uintptr_t *val, uint32_t *plen)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;

	/* Split the absolute path into up to 5 components ("...", no leading '/'
	 * from here on). Empty path or the root would match depth 0, which we
	 * never need: /<node>[/<node>] only. */
	const char *comp[5];
	int comp_len[5], ncomp = 0;
	{
		const char *c = path;
		while (*c == '/')
			c++;
		while (*c && ncomp < 5) {
			const char *e = c;
			while (*e && *e != '/')
				e++;
			comp[ncomp] = c;
			comp_len[ncomp] = e - c;
			ncomp++;
			while (*e == '/')
				e++;
			c = e;
		}
		if (!ncomp)
			return 0;
	}

	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, pm = 0;	/* pm = matched path components so far */

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			/* depth-2 = non-root ancestors so far; match when every
			 * previous component matched (depth 1 == root matches
			 * nothing). Final node always sits at depth ncomp+1. */
			if (pm < ncomp && pm == depth - 2 &&
			    node_match(name, comp[pm], comp_len[pm]))
				pm++;
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (pm == ncomp && depth == ncomp + 1)
				return 0;	/* left the node without the prop */
			if (pm == depth - 1)
				pm--;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t plen_ = be32(p), nameoff = be32(p + 4);
			uintptr_t val_ = p + 8;
			if (pm == ncomp && depth == ncomp + 1 &&
			    streq((const char *)(strings + nameoff), prop)) {
				*val = val_;
				*plen = plen_;
				return 1;
			}
			p = align4(val_ + plen_);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

int fdt_get_string(uintptr_t fdt, const char *path, const char *prop, char *out, int len)
{
	uintptr_t val;
	uint32_t plen;

	if (len <= 0 || !fdt_walk_prop(fdt, path, prop, &val, &plen))
		return 0;
	int i;
	for (i = 0; i < len - 1 && (int)plen > i && *(volatile char *)(val + i); i++)
		out[i] = *(volatile char *)(val + i);
	out[i] = '\0';
	if (i == len - 1)
		return 0;	/* truncated: reject */
	return 1;
}

int fdt_get_reg(uintptr_t fdt, const char *path, uint64_t *base, uint64_t *size)
{
	uintptr_t val;
	uint32_t plen;

	if (!fdt_walk_prop(fdt, path, "reg", &val, &plen))
		return 0;
	if (plen == 16) {		/* 2+2 cells: 64-bit address, 64-bit size */
		*base = (uint64_t)be32(val) << 32 | be32(val + 4);
		*size = (uint64_t)be32(val + 8) << 32 | be32(val + 12);
		return 1;
	}
	if (plen == 8) {		/* 1+1 cells: 32-bit address, 32-bit size */
		*base = be32(val);
		*size = be32(val + 4);
		return 1;
	}
	return 0;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}
