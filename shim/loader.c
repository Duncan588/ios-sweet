/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

/* word-of-4-bytes in DISPLAY order (byte0 first), so hex lines read like a
 * byte dump instead of endian-swapped words. */
static uint32_t pb(const volatile char *p, int i)
{
	return (uint32_t)(uint8_t)p[i] << 24 | (uint32_t)(uint8_t)p[i + 1] << 16 |
	       (uint32_t)(uint8_t)p[i + 2] << 8 | (uint32_t)(uint8_t)p[i + 3];
}

static void tag16(const char *base)
{
	static char t[5];
	const volatile char *p = (const volatile char *)base;
	if (p[0] == 'S' && p[1] == 'W') { t[0]='M'; t[1]='A'; t[2]='G'; }
	else if (p[0] == 0x07 && p[1] == 0x07) { t[0]='C'; t[1]='P'; t[2]='I'; t[3]='O'; }
	else if (p[0] == 0x1f && p[1] == 0x8b) { t[0]='G'; t[1]='Z'; t[2]='I'; t[3]='P'; }
	else if (!p[0] && !p[1]) { t[0]='0'; t[1]='0'; t[2]='s'; }
	else { t[0]='?'; t[1]='?'; t[2]='?'; }
	t[3] = ' '; t[4] = 0; con_str("", t);
}

static void dump_points(uint64_t rd_start, uint64_t rd_end)
{
	static const uint64_t off[] = {0, 0x1000, 0x100000, 0x1330000, 0x1336100,
		0x133A000, 0x1400000, 0 /*end-0x400 filled below*/};
	con_str("DUMP", "initrd hex (byte order, 16B per row):");
	volatile char *base = (volatile char *)rd_start;
	uint64_t n = rd_end - rd_start;
	for (int i = 0; i < 8; i++) {
		uint64_t o = off[i] ? off[i] : n - 0x400;
		if (o + 16 > n) o = n - 16;
		con_hex2("", "+0x", (uint32_t)o, "w0=", pb(base + o, 0));
		con_hex2("", "   w1=", pb(base + o, 4), "w2=", pb(base + o, 8));
		tag16(base + o);
	}
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);

	/* ABL prepends its own vendor ramdisk cpio before our payload, so the
	 * magic is NOT at the first bytes: linear-scan the whole initrd for
	 * "SWEETXNU". ~17 MiB at 4-byte stride is a few ms with the MMU off. */
	const struct payload *p = 0;
	uint64_t scanned;
	/* byte stride: a payload appended after the vendor cpio may sit at any
	 * alignment (pad/gravity unknown), so do not assume multiples of 4. */
	for (scanned = 0; scanned + 8 <= rd_end - rd_start; scanned++)
		if (magic_ok((const char *)(rd_start + scanned))) {
			p = (const struct payload *)(rd_start + scanned);
			break;
		}
	if (!p) {
		dump_points(rd_start, rd_end);
		return "no SWEETXNU payload (scanned all of initrd)";
	}
	con_hex2("INITRD", "payload found at +0x", (uint32_t)scanned, "size 0x", (uint32_t)(rd_end - rd_start));
	/* Header sanity BEFORE trusting any dependent field: a stray match in
	 * the vendor cpio (or a torn scan hit) must be rejected, not deref'd
	 * into wild copies. */
	if (p->version != 1 || p->nsegs == 0 || p->nsegs > 32 ||
	    p->virt_base == 0 || p->entry_va == 0 ||
	    p->phys_base == 0 || p->phys_base > 0x200000000ULL ||
	    p->mem_size == 0 || p->mem_size > 0x100000000ULL ||
	    p->kernel_end < p->virt_base || p->kernel_end > p->virt_base + 0x20000000ULL ||
	    (p->adt_off & 7) || ((rd_start + scanned) & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes: segs (which render
	 * as phys_base .. kernel_end), the ADT and the 16K-aligned boot_args page.
	 * p->mem_size is the *DRAM bank size* handed to XNU, NOT the kernel span
	 * -- comparing against it wrongly rejects a fine ramdisk (venus hit). */
	uintptr_t dest_end = (p->kernel_end + pa_of_va_base) + p->adt_size + 2 * PAGE_16K;
	if (rd_end > p->phys_base && rd_start < dest_end)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + scanned + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + scanned + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");
	/* S3 canary (real-device only): the last shim paint before the jump.
	 * Screen after this point answers two questions:
	 *   - if xnu's first kprintf wipes row 0 ("XNUNEXT" gone) -> xnu console
	 *     hit the SAME framebuffer (good): death is later, keep reading;
	 *   - if "JUMPXNU" lines are untouched for good -> xnu never drew
	 *     (early data abort / died before its console init: check the
	 *     boot_args Video below). These values are the ones xnu receives. */
	con_hex2("JUMPXNU", "ba=", (uint32_t)p->virt_base, "vbase=", FB_BASE);
	con_hex2("JUMPXNU", "pli=", (uint32_t)rd_start, "sz=", (uint32_t)(rd_end - rd_start));
	/* CANARY2: redraw ON row 0 as the very last act, so xnu's earliest
	 * console writes start at row 0 too and visibly overwrite it. */
	con_reset_row();
	con_str("XNUNEXT", "next writer is xnu kprintf on the same fb");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
