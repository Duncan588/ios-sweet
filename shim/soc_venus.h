#pragma once
/*
 * Xiaomi 11 (venus, SM8350/lahaina) framebuffer + SoC constants for the
 * venus probe shim. ALL VALUES ARE PLACEHOLDERS pending the real dump
 * analysis report (venus_boot_b.img etc.); fill them in before the first
 * real fastboot boot. TODO(dump) marks exactly what needs confirmation.
 */
#include <stdint.h>

/* TODO(dump): venus FB is expected in the reserved-memory splash region,
 * but neither address nor pixel format is confirmed for SM8350 yet. */
#define VENUS_FB_BASE		0x9d000000UL	/* TODO(dump): UNKNOWN */
#define VENUS_FB_W		1080		/* TODO(dump): UNKNOWN */
#define VENUS_FB_H		2400		/* TODO(dump): UNKNOWN */
#define VENUS_FB_STRIDE		(VENUS_FB_W * 4) /* a8r8g8b8, TODO(dump): UNKNOWN */

/* Probe banner string printed as "VENUS PROBE <string>". */
#define VENUS_PROBE_TAG		"venus-sm8350"
