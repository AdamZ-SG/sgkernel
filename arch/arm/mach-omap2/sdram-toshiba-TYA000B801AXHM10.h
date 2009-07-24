/*
 * SDRC register values for the Toshiba TYA000B801AXHM10
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARCH_ARM_MACH_OMAP2_SDRAM_TOSHIBA_TYA000B801AXHM10

#define ARCH_ARM_MACH_OMAP2_SDRAM_TOSHIBA_TYA000B801AXHM10

#include <mach/sdrc.h>

static struct omap_sdrc_params TYA000B801AXHM10_sdrc_params[] = {
    [0] = {
        .rate        = 160500000,
        .actim_ctrla = 0xBA9DB4C6,
        .actim_ctrlb = 0x00022221,
        .rfr_ctrl    = 0x0004B902,
        .mr      = 0x00000032,
    },
    [1] = {
        .rate        = 80250000,
        .actim_ctrla = 0x49512284,
        .actim_ctrlb = 0x0001120C,
        .rfr_ctrl    = 0x23F02,
        .mr      = 0x00000032,
    },
    [2] = {
        .rate        = 0
    },
};

#endif

