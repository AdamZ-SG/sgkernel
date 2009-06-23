/*
 * isppreview.c
 *
 * Driver Library for Preview module in TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contributors:
 *	Senthilvadivu Guruswamy <svadivu@ti.com>
 *	Pallavi Kulkarni <p-kulkarni@ti.com>
 *	Sergio Aguirre <saaguirre@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "isp.h"
#include "ispreg.h"
#include "isppreview.h"

static struct ispprev_nf prev_nf_t;
static struct prev_params *params;
static int rg_update, gg_update, bg_update, nf_enable, nf_update;

/* Structure for saving/restoring preview module registers */
static struct isp_reg ispprev_reg_list[] = {
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_HORZ_INFO, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_VERT_INFO, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RSDR_ADDR, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RADR_OFFSET, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_DSDR_ADDR, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_DRKF_OFFSET, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_WSDR_ADDR, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_WADD_OFFSET, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_AVE, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_HMED, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_NF, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_WB_DGAIN, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_WBGAIN, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_WBSEL, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CFA, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_BLKADJOFF, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT1, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT2, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT3, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT4, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT5, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF1, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF2, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC0, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC1, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC2, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC_OFFSET, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CSUP, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_SETUP_YC, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR0, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR1, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR2, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR3, 0x0000},
	{OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, 0x0000},
	{0, ISP_TOK_TERM, 0x0000}
};


/* Default values in Office Flourescent Light for RGBtoRGB Blending */
static struct ispprev_rgbtorgb flr_rgb2rgb = {
	{	/* RGB-RGB Matrix */
		{0x01E2, 0x0F30, 0x0FEE},
		{0x0F9B, 0x01AC, 0x0FB9},
		{0x0FE0, 0x0EC0, 0x0260}
	},	/* RGB Offset */
	{0x0000, 0x0000, 0x0000}
};

/* Default values in Office Flourescent Light for RGB to YUV Conversion*/
static struct ispprev_csc flr_prev_csc[] = {
	{
		{	/* CSC Coef Matrix */
			{66, 129, 25},
			{-38, -75, 112},
			{112, -94 , -18}
		},	/* CSC Offset */
		{0x0, 0x0, 0x0}
	},
	{
		{	/* CSC Coef Matrix BW */
			{66, 129, 25},
			{0, 0, 0},
			{0, 0, 0}
		},	/* CSC Offset */
		{0x0, 0x0, 0x0}
	},
	{
		{	/* CSC Coef Matrix Sepia */
			{19, 38, 7},
			{0, 0, 0},
			{0, 0, 0}
		},	/* CSC Offset */
		{0x0, 0xE7, 0x14}
	}
};


/* Default values in Office Flourescent Light for CFA Gradient*/
#define FLR_CFA_GRADTHRS_HORZ	0x28
#define FLR_CFA_GRADTHRS_VERT	0x28

/* Default values in Office Flourescent Light for Chroma Suppression*/
#define FLR_CSUP_GAIN		0x0D
#define FLR_CSUP_THRES		0xEB

/* Default values in Office Flourescent Light for Noise Filter*/
#define FLR_NF_STRGTH		0x03

/* Default values in Office Flourescent Light for White Balance*/
#define FLR_WBAL_DGAIN		0x100
#define FLR_WBAL_COEF0		0x20
#define FLR_WBAL_COEF1		0x29
#define FLR_WBAL_COEF2		0x2d
#define FLR_WBAL_COEF3		0x20

#define FLR_WBAL_COEF0_ES1	0x20
#define FLR_WBAL_COEF1_ES1	0x23
#define FLR_WBAL_COEF2_ES1	0x39
#define FLR_WBAL_COEF3_ES1	0x20

/* Default values in Office Flourescent Light for Black Adjustment*/
#define FLR_BLKADJ_BLUE		0x0
#define FLR_BLKADJ_GREEN	0x0
#define FLR_BLKADJ_RED		0x0

static int update_color_matrix;

/**
 * struct isp_prev - Structure for storing ISP Preview module information
 * @prev_inuse: Flag to determine if CCDC has been reserved or not (0 or 1).
 * @prevout_w: Preview output width.
 * @prevout_h: Preview output height.
 * @previn_w: Preview input width.
 * @previn_h: Preview input height.
 * @prev_inpfmt: Preview input format.
 * @prev_outfmt: Preview output format.
 * @hmed_en: Horizontal median filter enable.
 * @nf_en: Noise filter enable.
 * @dcor_en: Defect correction enable.
 * @cfa_en: Color Filter Array (CFA) interpolation enable.
 * @csup_en: Chrominance suppression enable.
 * @yenh_en: Luma enhancement enable.
 * @fmtavg: Number of horizontal pixels to average in input formatter. The
 *          input width should be a multiple of this number.
 * @brightness: Brightness in preview module.
 * @contrast: Contrast in preview module.
 * @color: Color effect in preview module.
 * @cfafmt: Color Filter Array (CFA) Format.
 * @ispprev_mutex: Mutex for isp preview.
 *
 * This structure is used to store the OMAP ISP Preview module Information.
 */
static struct isp_prev {
	int pm_state;
	u8 prev_inuse;
	u32 prevout_w;
	u32 prevout_h;
	u32 previn_w;
	u32 previn_h;
	enum preview_input prev_inpfmt;
	enum preview_output prev_outfmt;
	u8 hmed_en;
	u8 nf_en;
	u8 dcor_en;
	u8 cfa_en;
	u8 csup_en;
	u8 yenh_en;
	u8 fmtavg;
	u8 brightness;
	u8 contrast;
	enum v4l2_colorfx color;
	enum cfa_fmt cfafmt;
	struct mutex ispprev_mutex; /* For checking/modifying prev_inuse */
	u32 sph;
	u32 slv;
} ispprev_obj;

/* Saved parameters */
static struct prev_params *prev_config_params;

/*
 * Coeficient Tables for the submodules in Preview.
 * Array is initialised with the values from.the tables text file.
 */

/*
 * CFA Filter Coefficient Table
 *
 */
static u32 cfa_coef_table[] = {
#include "cfa_coef_table.h"
};

/*
 * Gamma Correction Table - Red
 */
static u32 redgamma_table[] = {
#include "redgamma_table.h"
};

/*
 * Gamma Correction Table - Green
 */
static u32 greengamma_table[] = {
#include "greengamma_table.h"
};

/*
 * Gamma Correction Table - Blue
 */
static u32 bluegamma_table[] = {
#include "bluegamma_table.h"
};

/*
 * Noise Filter Threshold table
 */
static u32 noise_filter_table[] = {
#include "noise_filter_table.h"
};

/*
 * Luminance Enhancement Table
 */
static u32 luma_enhance_table[] = {
#include "luma_enhance_table.h"
};

/**
 * omap34xx_isp_preview_config - Abstraction layer Preview configuration.
 * @userspace_add: Pointer from Userspace to structure with flags and data to
 *                 update.
 **/
int omap34xx_isp_preview_config(void *userspace_add)
{
	struct ispprev_hmed prev_hmed_t;
	struct ispprev_cfa prev_cfa_t;
	struct ispprev_csup csup_t;
	struct ispprev_wbal prev_wbal_t;
	struct ispprev_blkadj prev_blkadj_t;
	struct ispprev_rgbtorgb rgb2rgb_t;
	struct ispprev_csc prev_csc_t;
	struct ispprev_yclimit yclimit_t;
	struct ispprev_dcor prev_dcor_t;
	struct ispprv_update_config *preview_struct;
	struct isptables_update isp_table_update;
	int yen_t[ISPPRV_YENH_TBL_SIZE];

	if (userspace_add == NULL)
		return -EINVAL;

	preview_struct = userspace_add;

	if (ISP_ABS_PREV_LUMAENH & preview_struct->flag) {
		if (ISP_ABS_PREV_LUMAENH & preview_struct->update) {
			if (copy_from_user(yen_t, preview_struct->yen,
					   sizeof(yen_t)))
				goto err_copy_from_user;
			isppreview_config_luma_enhancement(yen_t);
		}
		params->features |= PREV_LUMA_ENHANCE;
	} else if (ISP_ABS_PREV_LUMAENH & preview_struct->update)
		params->features &= ~PREV_LUMA_ENHANCE;

	if (ISP_ABS_PREV_INVALAW & preview_struct->flag) {
		isppreview_enable_invalaw(1);
		params->features |= PREV_INVERSE_ALAW;
	} else {
		isppreview_enable_invalaw(0);
		params->features &= ~PREV_INVERSE_ALAW;
	}

	if (ISP_ABS_PREV_HRZ_MED & preview_struct->flag) {
		if (ISP_ABS_PREV_HRZ_MED & preview_struct->update) {
			if (copy_from_user(&prev_hmed_t,
					   (struct ispprev_hmed *)
					   preview_struct->prev_hmed,
					   sizeof(struct ispprev_hmed)))
				goto err_copy_from_user;
			isppreview_config_hmed(prev_hmed_t);
		}
		isppreview_enable_hmed(1);
		params->features |= PREV_HORZ_MEDIAN_FILTER;
	} else if (ISP_ABS_PREV_HRZ_MED & preview_struct->update) {
		isppreview_enable_hmed(0);
		params->features &= ~PREV_HORZ_MEDIAN_FILTER;
	}

	if (ISP_ABS_PREV_CFA & preview_struct->flag) {
		if (ISP_ABS_PREV_CFA & preview_struct->update) {
			if (copy_from_user(&prev_cfa_t,
					   (struct ispprev_cfa *)
					   preview_struct->prev_cfa,
					   sizeof(struct ispprev_cfa)))
				goto err_copy_from_user;

			isppreview_config_cfa(prev_cfa_t);
		}
		isppreview_enable_cfa(1);
		params->features |= PREV_CFA;
	} else if (ISP_ABS_PREV_CFA & preview_struct->update) {
		isppreview_enable_cfa(0);
		params->features &= ~PREV_CFA;
	}

	if (ISP_ABS_PREV_CHROMA_SUPP & preview_struct->flag) {
		if (ISP_ABS_PREV_CHROMA_SUPP & preview_struct->update) {
			if (copy_from_user(&csup_t,
					   (struct ispprev_csup *)
					   preview_struct->csup,
					   sizeof(struct ispprev_csup)))
				goto err_copy_from_user;
			isppreview_config_chroma_suppression(csup_t);
		}
		isppreview_enable_chroma_suppression(1);
		params->features |= PREV_CHROMA_SUPPRESS;
	} else if (ISP_ABS_PREV_CHROMA_SUPP & preview_struct->update) {
		isppreview_enable_chroma_suppression(0);
		params->features &= ~PREV_CHROMA_SUPPRESS;
	}

	if (ISP_ABS_PREV_WB & preview_struct->update) {
		if (copy_from_user(&prev_wbal_t, (struct ispprev_wbal *)
				   preview_struct->prev_wbal,
				   sizeof(struct ispprev_wbal)))
			goto err_copy_from_user;
		isppreview_config_whitebalance(prev_wbal_t);
	}

	if (ISP_ABS_PREV_BLKADJ & preview_struct->update) {
		if (copy_from_user(&prev_blkadj_t, (struct ispprev_blkadjl *)
				   preview_struct->prev_blkadj,
				   sizeof(struct ispprev_blkadj)))
			goto err_copy_from_user;
		isppreview_config_blkadj(prev_blkadj_t);
	}

	if (ISP_ABS_PREV_RGB2RGB & preview_struct->update) {
		if (copy_from_user(&rgb2rgb_t, (struct ispprev_rgbtorgb *)
				   preview_struct->rgb2rgb,
				   sizeof(struct ispprev_rgbtorgb)))
			goto err_copy_from_user;
		isppreview_config_rgb_blending(rgb2rgb_t);
	}

	if (ISP_ABS_PREV_COLOR_CONV & preview_struct->update) {
		if (copy_from_user(&prev_csc_t, (struct ispprev_csc *)
				   preview_struct->prev_csc,
				   sizeof(struct ispprev_csc)))
			goto err_copy_from_user;
		isppreview_config_rgb_to_ycbcr(prev_csc_t);
	}

	if (ISP_ABS_PREV_YC_LIMIT & preview_struct->update) {
		if (copy_from_user(&yclimit_t, (struct ispprev_yclimit *)
				   preview_struct->yclimit,
				   sizeof(struct ispprev_yclimit)))
			goto err_copy_from_user;
		isppreview_config_yc_range(yclimit_t);
	}

	if (ISP_ABS_PREV_DEFECT_COR & preview_struct->flag) {
		if (ISP_ABS_PREV_DEFECT_COR & preview_struct->update) {
			if (copy_from_user(&prev_dcor_t,
					   (struct ispprev_dcor *)
					   preview_struct->prev_dcor,
					   sizeof(struct ispprev_dcor)))
				goto err_copy_from_user;
			isppreview_config_dcor(prev_dcor_t);
		}
		isppreview_enable_dcor(1);
		params->features |= PREV_DEFECT_COR;
	} else if (ISP_ABS_PREV_DEFECT_COR & preview_struct->update) {
		isppreview_enable_dcor(0);
		params->features &= ~PREV_DEFECT_COR;
	}

	if (ISP_ABS_PREV_GAMMABYPASS & preview_struct->flag) {
		isppreview_enable_gammabypass(1);
		params->features |= PREV_GAMMA_BYPASS;
	} else {
		isppreview_enable_gammabypass(0);
		params->features &= ~PREV_GAMMA_BYPASS;
	}

	isp_table_update.update = preview_struct->update;
	isp_table_update.flag = preview_struct->flag;
	isp_table_update.prev_nf = preview_struct->prev_nf;
	isp_table_update.red_gamma = preview_struct->red_gamma;
	isp_table_update.green_gamma = preview_struct->green_gamma;
	isp_table_update.blue_gamma = preview_struct->blue_gamma;

	if (omap34xx_isp_tables_update(&isp_table_update))
		goto err_copy_from_user;

	return 0;

err_copy_from_user:
	printk(KERN_ERR "Preview Config: Copy From User Error\n");
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(omap34xx_isp_preview_config);

/**
 * omap34xx_isp_tables_update - Abstraction layer Tables update.
 * @isptables_struct: Pointer from Userspace to structure with flags and table
 *                 data to update.
 **/
int omap34xx_isp_tables_update(struct isptables_update *isptables_struct)
{

	if (ISP_ABS_TBL_NF & isptables_struct->flag) {
		nf_enable = 1;
		params->features |= PREV_NOISE_FILTER;
		if (ISP_ABS_TBL_NF & isptables_struct->update) {
			if (copy_from_user(&prev_nf_t, (struct ispprev_nf *)
					   isptables_struct->prev_nf,
					   sizeof(struct ispprev_nf)))
				goto err_copy_from_user;

			nf_update = 1;
		} else
			nf_update = 0;
	} else {
		nf_enable = 0;
		params->features &= ~PREV_NOISE_FILTER;
		if (ISP_ABS_TBL_NF & isptables_struct->update)
			nf_update = 1;
		else
			nf_update = 0;
	}

	if (ISP_ABS_TBL_REDGAMMA & isptables_struct->update) {
		if (copy_from_user(redgamma_table, isptables_struct->red_gamma,
				   sizeof(redgamma_table))) {
			goto err_copy_from_user;
		}
		rg_update = 1;
	} else
		rg_update = 0;

	if (ISP_ABS_TBL_GREENGAMMA & isptables_struct->update) {
		if (copy_from_user(greengamma_table,
				   isptables_struct->green_gamma,
				   sizeof(greengamma_table)))
			goto err_copy_from_user;
		gg_update = 1;
	} else
		gg_update = 0;

	if (ISP_ABS_TBL_BLUEGAMMA & isptables_struct->update) {
		if (copy_from_user(bluegamma_table,
				   isptables_struct->blue_gamma,
				   sizeof(bluegamma_table))) {
			goto err_copy_from_user;
		}
		bg_update = 1;
	} else
		bg_update = 0;

	return 0;

err_copy_from_user:
	printk(KERN_ERR "Preview Tables:Copy From User Error\n");
	return -EFAULT;
}

/**
 * isppreview_config_shadow_registers - Program shadow registers for preview.
 *
 * Allows user to program shadow registers associated with preview module.
 **/
void isppreview_config_shadow_registers()
{
	u8 current_brightness_contrast;
	int ctr, prv_disabled;

	isppreview_query_brightness(&current_brightness_contrast);
	if (current_brightness_contrast !=
	    (ispprev_obj.brightness * ISPPRV_BRIGHT_UNITS)) {
		DPRINTK_ISPPREV(" Changing Brightness level to %d\n",
				ispprev_obj.brightness);
		isppreview_config_brightness(ispprev_obj.brightness *
					     ISPPRV_BRIGHT_UNITS);
	}

	isppreview_query_contrast(&current_brightness_contrast);
	if (current_brightness_contrast !=
	    (ispprev_obj.contrast * ISPPRV_CONTRAST_UNITS)) {
		DPRINTK_ISPPREV(" Changing Contrast level to %d\n",
				ispprev_obj.contrast);
		isppreview_config_contrast(ispprev_obj.contrast *
					   ISPPRV_CONTRAST_UNITS);
	}
	if (update_color_matrix) {
		isppreview_config_rgb_to_ycbcr(flr_prev_csc[ispprev_obj.color]);
		update_color_matrix = 0;
	}
	if (gg_update || rg_update || bg_update || nf_update) {
		isppreview_enable(0);
		prv_disabled = 1;
	}

	if (gg_update) {
		isp_reg_writel(ISPPRV_TBL_ADDR_GREEN_G_START,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);

		for (ctr = 0; ctr < ISP_GAMMA_TABLE_SIZE; ctr++) {
			isp_reg_writel(greengamma_table[ctr],
				       OMAP3_ISP_IOMEM_PREV,
				       ISPPRV_SET_TBL_DATA);
		}
		gg_update = 0;
	}

	if (rg_update) {
		isp_reg_writel(ISPPRV_TBL_ADDR_RED_G_START,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);

		for (ctr = 0; ctr < ISP_GAMMA_TABLE_SIZE; ctr++) {
			isp_reg_writel(redgamma_table[ctr],
				       OMAP3_ISP_IOMEM_PREV,
				       ISPPRV_SET_TBL_DATA);
		}
		rg_update = 0;
	}

	if (bg_update) {
		isp_reg_writel(ISPPRV_TBL_ADDR_BLUE_G_START,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);

		for (ctr = 0; ctr < ISP_GAMMA_TABLE_SIZE; ctr++) {
			isp_reg_writel(bluegamma_table[ctr],
				       OMAP3_ISP_IOMEM_PREV,
				       ISPPRV_SET_TBL_DATA);
		}
		bg_update = 0;
	}

	if (nf_update && nf_enable) {
		isp_reg_writel(0xC00,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
		isp_reg_writel(prev_nf_t.spread,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_NF);
		for (ctr = 0; ctr < ISPPRV_NF_TBL_SIZE; ctr++) {
			isp_reg_writel(prev_nf_t.table[ctr],
				       OMAP3_ISP_IOMEM_PREV,
				       ISPPRV_SET_TBL_DATA);
		}
		isppreview_enable_noisefilter(1);
		nf_update = 0;
	}

	if (~nf_update && nf_enable)
		isppreview_enable_noisefilter(1);

	if (nf_update && ~nf_enable)
		isppreview_enable_noisefilter(0);

	if (prv_disabled) {
		isppreview_enable(1);
		prv_disabled = 0;
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_shadow_registers);

/**
 * isppreview_request - Reserves the preview module.
 *
 * Returns 0 if successful, or -EBUSY if the module was already reserved.
 **/
int isppreview_request()
{
	mutex_lock(&ispprev_obj.ispprev_mutex);
	if (ispprev_obj.prev_inuse) {
		mutex_unlock(&ispprev_obj.ispprev_mutex);
		printk(KERN_ERR "ISP_ERR : Preview Module Busy\n");
		return -EBUSY;
	}
	ispprev_obj.prev_inuse = 1;
	mutex_unlock(&ispprev_obj.ispprev_mutex);
	isp_reg_or(OMAP3_ISP_IOMEM_MAIN, ISP_CTRL, ISPCTRL_PREV_RAM_EN |
		   ISPCTRL_PREV_CLK_EN |
		   ISPCTRL_SBL_WR1_RAM_EN);
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_request);

/**
 * isppreview_free - Frees the preview module.
 *
 * Returns 0 if successful, or -EINVAL if the module was already freed.
 **/
int isppreview_free()
{
	mutex_lock(&ispprev_obj.ispprev_mutex);
	if (ispprev_obj.prev_inuse) {
		ispprev_obj.prev_inuse = 0;
		mutex_unlock(&ispprev_obj.ispprev_mutex);
		isp_reg_and(OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
			    ~(ISPCTRL_PREV_CLK_EN |
			      ISPCTRL_PREV_RAM_EN |
			      ISPCTRL_SBL_WR1_RAM_EN));
		return 0;
	} else {
		mutex_unlock(&ispprev_obj.ispprev_mutex);
		DPRINTK_ISPPREV("ISP_ERR : Preview Module already freed\n");
		return -EINVAL;
	}

}
EXPORT_SYMBOL_GPL(isppreview_free);

/** isppreview_config_datapath - Specifies input and output modules for Preview
 * @input: Indicates the module that gives the image to preview.
 * @output: Indicates the module to which the preview outputs to.
 *
 * Configures the default configuration for the CCDC to work with.
 *
 * The valid values for the input are PRV_RAW_CCDC (0), PRV_RAW_MEM (1),
 * PRV_RGBBAYERCFA (2), PRV_COMPCFA (3), PRV_CCDC_DRKF (4), PRV_OTHERS (5).
 *
 * The valid values for the output are PREVIEW_RSZ (0), PREVIEW_MEM (1).
 *
 * Returns 0 if successful, or -EINVAL if wrong input or output values are
 * specified.
 **/
int isppreview_config_datapath(enum preview_input input,
			       enum preview_output output)
{
	u32 pcr = 0;
	u8 enable = 0;
	struct prev_params *params = prev_config_params;
	struct ispprev_yclimit yclimit;

	pcr = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);

	switch (input) {
	case PRV_RAW_CCDC:
		pcr &= ~ISPPRV_PCR_SOURCE;
		pcr &= ~ISPPRV_PCR_ONESHOT;
		ispprev_obj.prev_inpfmt = PRV_RAW_CCDC;
		break;
	case PRV_RAW_MEM:
		pcr |= ISPPRV_PCR_SOURCE;
		pcr |= ISPPRV_PCR_ONESHOT;
		ispprev_obj.prev_inpfmt = PRV_RAW_MEM;
		break;
	case PRV_CCDC_DRKF:
		pcr |= ISPPRV_PCR_DRKFCAP;
		pcr |= ISPPRV_PCR_ONESHOT;
		ispprev_obj.prev_inpfmt = PRV_CCDC_DRKF;
		break;
	case PRV_COMPCFA:
		ispprev_obj.prev_inpfmt = PRV_COMPCFA;
		break;
	case PRV_OTHERS:
		ispprev_obj.prev_inpfmt = PRV_OTHERS;
		break;
	case PRV_RGBBAYERCFA:
		ispprev_obj.prev_inpfmt = PRV_RGBBAYERCFA;
		break;
	default:
		printk(KERN_ERR "ISP_ERR : Wrong Input\n");
		return -EINVAL;
	};

	switch (output) {
	case PREVIEW_RSZ:
		pcr |= ISPPRV_PCR_RSZPORT;
		pcr &= ~ISPPRV_PCR_SDRPORT;
		break;
	case PREVIEW_MEM:
		pcr &= ~ISPPRV_PCR_RSZPORT;
		pcr |= ISPPRV_PCR_SDRPORT;
		break;
	default:
		printk(KERN_ERR "ISP_ERR : Wrong Output\n");
		return -EINVAL;
	}
	ispprev_obj.prev_outfmt = output;

	isp_reg_writel(pcr, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);

	isppreview_config_ycpos(params->pix_fmt);

	if (params->cfa.cfa_table != NULL)
		isppreview_config_cfa(params->cfa);
	if (params->csup.hypf_en == 1)
		isppreview_config_chroma_suppression(params->csup);
	if (params->ytable != NULL)
		isppreview_config_luma_enhancement(params->ytable);

	if (params->gtable.redtable != NULL)
		isppreview_config_gammacorrn(params->gtable);

	enable = (params->features & PREV_CFA) ? 1 : 0;
	isppreview_enable_cfa(enable);

	enable = (params->features & PREV_CHROMA_SUPPRESS) ? 1 : 0;
	isppreview_enable_chroma_suppression(enable);

	enable = (params->features & PREV_LUMA_ENHANCE) ? 1 : 0;
	isppreview_enable_luma_enhancement(enable);

	enable = (params->features & PREV_NOISE_FILTER) ? 1 : 0;
	if (enable)
		isppreview_config_noisefilter(params->nf);
	isppreview_enable_noisefilter(enable);

	enable = (params->features & PREV_DEFECT_COR) ? 1 : 0;
	if (enable)
		isppreview_config_dcor(params->dcor);
	isppreview_enable_dcor(enable);

	enable = (params->features & PREV_GAMMA_BYPASS) ? 1 : 0;
	isppreview_enable_gammabypass(enable);

	isppreview_config_whitebalance(params->wbal);
	isppreview_config_blkadj(params->blk_adj);
	isppreview_config_rgb_blending(params->rgb2rgb);
	isppreview_config_rgb_to_ycbcr(params->rgb2ycbcr);

	isppreview_config_contrast(params->contrast * ISPPRV_CONTRAST_UNITS);
	isppreview_config_brightness(params->brightness * ISPPRV_BRIGHT_UNITS);

	yclimit.minC = ISPPRV_YC_MIN;
	yclimit.maxC = ISPPRV_YC_MAX;
	yclimit.minY = ISPPRV_YC_MIN;
	yclimit.maxY = ISPPRV_YC_MAX;
	isppreview_config_yc_range(yclimit);

	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_config_datapath);

/**
 * isppreview_set_skip - Set the number of rows/columns that should be skipped.
 *  h - Start Pixel Horizontal.
 *  v - Start Line Vertical.
 **/
void isppreview_set_skip(u32 h, u32 v)
{
	ispprev_obj.sph = h;
	ispprev_obj.slv = v;
}
EXPORT_SYMBOL_GPL(isppreview_set_skip);

/**
 * isppreview_config_ycpos - Configure byte layout of YUV image.
 * @mode: Indicates the required byte layout.
 **/
void isppreview_config_ycpos(enum preview_ycpos_mode mode)
{
	u32 pcr = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);
	pcr &= ~ISPPRV_PCR_YCPOS_CrYCbY;
	pcr |= (mode << ISPPRV_PCR_YCPOS_SHIFT);
	isp_reg_writel(pcr, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);
}
EXPORT_SYMBOL_GPL(isppreview_config_ycpos);

/**
 * isppreview_config_averager - Enable / disable / configure averager
 * @average: Average value to be configured.
 **/
void isppreview_config_averager(u8 average)
{
	int reg = 0;

	reg = AVE_ODD_PIXEL_DIST | AVE_EVEN_PIXEL_DIST | average;
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_PREV, ISPPRV_AVE);
}
EXPORT_SYMBOL_GPL(isppreview_config_averager);

/**
 * isppreview_enable_invalaw - Enable/Disable Inverse A-Law module in Preview.
 * @enable: 1 - Reverse the A-Law done in CCDC.
 **/
void isppreview_enable_invalaw(u8 enable)
{
	u32 pcr_val = 0;
	pcr_val = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);

	if (enable) {
		isp_reg_writel(pcr_val | ISPPRV_PCR_WIDTH | ISPPRV_PCR_INVALAW,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);
	} else {
		isp_reg_writel(pcr_val &
			       ~(ISPPRV_PCR_WIDTH | ISPPRV_PCR_INVALAW),
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);
	}
}
EXPORT_SYMBOL_GPL(isppreview_enable_invalaw);

/**
 * isppreview_enable_drkframe - Enable/Disable of the darkframe subtract.
 * @enable: 1 - Acquires memory bandwidth since the pixels in each frame is
 *          subtracted with the pixels in the current frame.
 *
 * The proccess is applied for each captured frame.
 **/
void isppreview_enable_drkframe(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_DRKFEN);
	else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_DRKFEN);
	}
}
EXPORT_SYMBOL_GPL(isppreview_enable_drkframe);

/**
 * isppreview_enable_shadcomp - Enables/Disables the shading compensation.
 * @enable: 1 - Enables the shading compensation.
 *
 * If dark frame subtract won't be used, then enable this shading
 * compensation.
 **/
void isppreview_enable_shadcomp(u8 enable)
{

	if (enable) {
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			   ISPPRV_PCR_SCOMP_EN);
		isppreview_enable_drkframe(1);
	} else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_SCOMP_EN);
	}
}
EXPORT_SYMBOL_GPL(isppreview_enable_shadcomp);

/**
 * isppreview_config_drkf_shadcomp - Configures shift value in shading comp.
 * @scomp_shtval: 3bit value of shift used in shading compensation.
 **/
void isppreview_config_drkf_shadcomp(u8 scomp_shtval)
{
	u32 pcr_val = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);

	pcr_val &= ISPPRV_PCR_SCOMP_SFT_MASK;
	isp_reg_writel(pcr_val | (scomp_shtval << ISPPRV_PCR_SCOMP_SFT_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR);
}
EXPORT_SYMBOL_GPL(isppreview_config_drkf_shadcomp);

/**
 * isppreview_enable_hmed - Enables/Disables of the Horizontal Median Filter.
 * @enable: 1 - Enables Horizontal Median Filter.
 **/
void isppreview_enable_hmed(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_HMEDEN);
	else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_HMEDEN);
	}
	ispprev_obj.hmed_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_hmed);

/**
 * isppreview_config_hmed - Configures the Horizontal Median Filter.
 * @prev_hmed: Structure containing the odd and even distance between the
 *             pixels in the image along with the filter threshold.
 **/
void isppreview_config_hmed(struct ispprev_hmed prev_hmed)
{

	u32 odddist = 0;
	u32 evendist = 0;

	if (prev_hmed.odddist == 1)
		odddist = ~ISPPRV_HMED_ODDDIST;
	else
		odddist = ISPPRV_HMED_ODDDIST;

	if (prev_hmed.evendist == 1)
		evendist = ~ISPPRV_HMED_EVENDIST;
	else
		evendist = ISPPRV_HMED_EVENDIST;

	isp_reg_writel(odddist | evendist | (prev_hmed.thres <<
					     ISPPRV_HMED_THRESHOLD_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_HMED);

}
EXPORT_SYMBOL_GPL(isppreview_config_hmed);

/**
 * isppreview_config_noisefilter - Configures the Noise Filter.
 * @prev_nf: Structure containing the noisefilter table, strength to be used
 *           for the noise filter and the defect correction enable flag.
 **/
void isppreview_config_noisefilter(struct ispprev_nf prev_nf)
{
	int i = 0;

	isp_reg_writel(prev_nf.spread, OMAP3_ISP_IOMEM_PREV, ISPPRV_NF);
	isp_reg_writel(ISPPRV_NF_TABLE_ADDR, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < ISPPRV_NF_TBL_SIZE; i++) {
		isp_reg_writel(prev_nf.table[i], OMAP3_ISP_IOMEM_PREV,
			       ISPPRV_SET_TBL_DATA);
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_noisefilter);

/**
 * isppreview_config_dcor - Configures the defect correction
 * @prev_nf: Structure containing the defect correction structure
 **/
void isppreview_config_dcor(struct ispprev_dcor prev_dcor)
{
	if (prev_dcor.couplet_mode_en) {
		isp_reg_writel(prev_dcor.detect_correct[0],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR0);
		isp_reg_writel(prev_dcor.detect_correct[1],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR1);
		isp_reg_writel(prev_dcor.detect_correct[2],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR2);
		isp_reg_writel(prev_dcor.detect_correct[3],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR3);
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_DCCOUP);
	} else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_DCCOUP);
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_dcor);

/**
 * isppreview_config_cfa - Configures the CFA Interpolation parameters.
 * @prev_cfa: Structure containing the CFA interpolation table, CFA format
 *            in the image, vertical and horizontal gradient threshold.
 **/
void isppreview_config_cfa(struct ispprev_cfa prev_cfa)
{
	int i = 0;

	ispprev_obj.cfafmt = prev_cfa.cfafmt;

	isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
		   (prev_cfa.cfafmt << ISPPRV_PCR_CFAFMT_SHIFT));

	isp_reg_writel(
		(prev_cfa.cfa_gradthrs_vert << ISPPRV_CFA_GRADTH_VER_SHIFT) |
		(prev_cfa.cfa_gradthrs_horz << ISPPRV_CFA_GRADTH_HOR_SHIFT),
		OMAP3_ISP_IOMEM_PREV, ISPPRV_CFA);

	isp_reg_writel(ISPPRV_CFA_TABLE_ADDR, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_SET_TBL_ADDR);

	for (i = 0; i < ISPPRV_CFA_TBL_SIZE; i++) {
		isp_reg_writel(prev_cfa.cfa_table[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_cfa);

/**
 * isppreview_config_gammacorrn - Configures the Gamma Correction table values
 * @gtable: Structure containing the table for red, blue, green gamma table.
 **/
void isppreview_config_gammacorrn(struct ispprev_gtable gtable)
{
	int i = 0;

	isp_reg_writel(ISPPRV_REDGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < ISPPRV_GAMMA_TBL_SIZE; i++) {
		isp_reg_writel(gtable.redtable[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}

	isp_reg_writel(ISPPRV_GREENGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < ISPPRV_GAMMA_TBL_SIZE; i++) {
		isp_reg_writel(gtable.greentable[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}

	isp_reg_writel(ISPPRV_BLUEGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < ISPPRV_GAMMA_TBL_SIZE; i++) {
		isp_reg_writel(gtable.bluetable[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_gammacorrn);

/**
 * isppreview_config_luma_enhancement - Sets the Luminance Enhancement table.
 * @ytable: Structure containing the table for Luminance Enhancement table.
 **/
void isppreview_config_luma_enhancement(u32 *ytable)
{
	int i = 0;

	isp_reg_writel(ISPPRV_YENH_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < ISPPRV_YENH_TBL_SIZE; i++) {
		isp_reg_writel(ytable[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}
}
EXPORT_SYMBOL_GPL(isppreview_config_luma_enhancement);

/**
 * isppreview_config_chroma_suppression - Configures the Chroma Suppression.
 * @csup: Structure containing the threshold value for suppression
 *        and the hypass filter enable flag.
 **/
void isppreview_config_chroma_suppression(struct ispprev_csup csup)
{
	isp_reg_writel(csup.gain | (csup.thres << ISPPRV_CSUP_THRES_SHIFT) |
		       (csup.hypf_en << ISPPRV_CSUP_HPYF_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CSUP);
}
EXPORT_SYMBOL_GPL(isppreview_config_chroma_suppression);

/**
 * isppreview_enable_noisefilter - Enables/Disables the Noise Filter.
 * @enable: 1 - Enables the Noise Filter.
 **/
void isppreview_enable_noisefilter(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_NFEN);
	else
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ~ISPPRV_PCR_NFEN);
	ispprev_obj.nf_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_noisefilter);

/**
 * isppreview_enable_dcor - Enables/Disables the defect correction.
 * @enable: 1 - Enables the defect correction.
 **/
void isppreview_enable_dcor(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_DCOREN);
	else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_DCOREN);
	}
	ispprev_obj.dcor_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_dcor);

/**
 * isppreview_enable_cfa - Enable/Disable the CFA Interpolation.
 * @enable: 1 - Enables the CFA.
 **/
void isppreview_enable_cfa(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_CFAEN);
	else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_CFAEN);
	}
	ispprev_obj.cfa_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_cfa);

/**
 * isppreview_enable_gammabypass - Enables/Disables the GammaByPass
 * @enable: 1 - Bypasses Gamma - 10bit input is cropped to 8MSB.
 *          0 - Goes through Gamma Correction. input and output is 10bit.
 **/
void isppreview_enable_gammabypass(u8 enable)
{
	if (enable) {
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			   ISPPRV_PCR_GAMMA_BYPASS);
	} else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_GAMMA_BYPASS);
	}
}
EXPORT_SYMBOL_GPL(isppreview_enable_gammabypass);

/**
 * isppreview_enable_luma_enhancement - Enables/Disables Luminance Enhancement
 * @enable: 1 - Enable the Luminance Enhancement.
 **/
void isppreview_enable_luma_enhancement(u8 enable)
{
	if (enable) {
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			   ISPPRV_PCR_YNENHEN);
	} else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_YNENHEN);
	}
	ispprev_obj.yenh_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_luma_enhancement);

/**
 * isppreview_enable_chroma_suppression - Enables/Disables Chrominance Suppr.
 * @enable: 1 - Enable the Chrominance Suppression.
 **/
void isppreview_enable_chroma_suppression(u8 enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_SUPEN);
	else {
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ~ISPPRV_PCR_SUPEN);
	}
	ispprev_obj.csup_en = enable ? 1 : 0;
}
EXPORT_SYMBOL_GPL(isppreview_enable_chroma_suppression);

/**
 * isppreview_config_whitebalance - Configures the White Balance parameters.
 * @prev_wbal: Structure containing the digital gain and white balance
 *             coefficient.
 *
 * Coefficient matrix always with default values.
 **/
void isppreview_config_whitebalance(struct ispprev_wbal prev_wbal)
{
	u32 val;

	isp_reg_writel(prev_wbal.dgain, OMAP3_ISP_IOMEM_PREV, ISPPRV_WB_DGAIN);

	val = prev_wbal.coef0 << ISPPRV_WBGAIN_COEF0_SHIFT;
	val |= prev_wbal.coef1 << ISPPRV_WBGAIN_COEF1_SHIFT;
	val |= prev_wbal.coef2 << ISPPRV_WBGAIN_COEF2_SHIFT;
	val |= prev_wbal.coef3 << ISPPRV_WBGAIN_COEF3_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_WBGAIN);

	isp_reg_writel(ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N0_0_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N0_1_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N0_2_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N0_3_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N1_0_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N1_1_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N1_2_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N1_3_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N2_0_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N2_1_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N2_2_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N2_3_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N3_0_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N3_1_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N3_2_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N3_3_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_WBSEL);
}
EXPORT_SYMBOL_GPL(isppreview_config_whitebalance);

/**
 * isppreview_config_whitebalance2 - Configures the White Balance parameters.
 * @prev_wbal: Structure containing the digital gain and white balance
 *             coefficient.
 *
 * Coefficient matrix can be changed.
 **/
void isppreview_config_whitebalance2(struct prev_white_balance prev_wbal)
{
	isp_reg_writel(prev_wbal.wb_dgain,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_WB_DGAIN);
	isp_reg_writel(prev_wbal.wb_gain[0] |
		       prev_wbal.wb_gain[1] << ISPPRV_WBGAIN_COEF1_SHIFT |
		       prev_wbal.wb_gain[2] << ISPPRV_WBGAIN_COEF2_SHIFT |
		       prev_wbal.wb_gain[3] << ISPPRV_WBGAIN_COEF3_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_WBGAIN);

	isp_reg_writel(
		prev_wbal.wb_coefmatrix[0][0] << ISPPRV_WBSEL_N0_0_SHIFT |
		prev_wbal.wb_coefmatrix[0][1] << ISPPRV_WBSEL_N0_1_SHIFT |
		prev_wbal.wb_coefmatrix[0][2] << ISPPRV_WBSEL_N0_2_SHIFT |
		prev_wbal.wb_coefmatrix[0][3] << ISPPRV_WBSEL_N0_3_SHIFT |
		prev_wbal.wb_coefmatrix[1][0] << ISPPRV_WBSEL_N1_0_SHIFT |
		prev_wbal.wb_coefmatrix[1][1] << ISPPRV_WBSEL_N1_1_SHIFT |
		prev_wbal.wb_coefmatrix[1][2] << ISPPRV_WBSEL_N1_2_SHIFT |
		prev_wbal.wb_coefmatrix[1][3] << ISPPRV_WBSEL_N1_3_SHIFT |
		prev_wbal.wb_coefmatrix[2][0] << ISPPRV_WBSEL_N2_0_SHIFT |
		prev_wbal.wb_coefmatrix[2][1] << ISPPRV_WBSEL_N2_1_SHIFT |
		prev_wbal.wb_coefmatrix[2][2] << ISPPRV_WBSEL_N2_2_SHIFT |
		prev_wbal.wb_coefmatrix[2][3] << ISPPRV_WBSEL_N2_3_SHIFT |
		prev_wbal.wb_coefmatrix[3][0] << ISPPRV_WBSEL_N3_0_SHIFT |
		prev_wbal.wb_coefmatrix[3][1] << ISPPRV_WBSEL_N3_1_SHIFT |
		prev_wbal.wb_coefmatrix[3][2] << ISPPRV_WBSEL_N3_2_SHIFT |
		prev_wbal.wb_coefmatrix[3][3] << ISPPRV_WBSEL_N3_3_SHIFT,
		OMAP3_ISP_IOMEM_PREV, ISPPRV_WBSEL);
}
EXPORT_SYMBOL_GPL(isppreview_config_whitebalance2);

/**
 * isppreview_config_blkadj - Configures the Black Adjustment parameters.
 * @prev_blkadj: Structure containing the black adjustment towards red, green,
 *               blue.
 **/
void isppreview_config_blkadj(struct ispprev_blkadj prev_blkadj)
{
	isp_reg_writel(prev_blkadj.blue |
		       (prev_blkadj.green << ISPPRV_BLKADJOFF_G_SHIFT) |
		       (prev_blkadj.red << ISPPRV_BLKADJOFF_R_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_BLKADJOFF);
}
EXPORT_SYMBOL_GPL(isppreview_config_blkadj);

/**
 * isppreview_config_rgb_blending - Configures the RGB-RGB Blending matrix.
 * @rgb2rgb: Structure containing the rgb to rgb blending matrix and the rgb
 *           offset.
 **/
void isppreview_config_rgb_blending(struct ispprev_rgbtorgb rgb2rgb)
{
	u32 val = 0;

	val = (rgb2rgb.matrix[0][0] & 0xfff) << ISPPRV_RGB_MAT1_MTX_RR_SHIFT;
	val |= (rgb2rgb.matrix[0][1] & 0xfff) << ISPPRV_RGB_MAT1_MTX_GR_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT1);

	val = (rgb2rgb.matrix[0][2] & 0xfff) << ISPPRV_RGB_MAT2_MTX_BR_SHIFT;
	val |= (rgb2rgb.matrix[1][0] & 0xfff) << ISPPRV_RGB_MAT2_MTX_RG_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT2);

	val = (rgb2rgb.matrix[1][1] & 0xfff) << ISPPRV_RGB_MAT3_MTX_GG_SHIFT;
	val |= (rgb2rgb.matrix[1][2] & 0xfff) << ISPPRV_RGB_MAT3_MTX_BG_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT3);

	val = (rgb2rgb.matrix[2][0] & 0xfff) << ISPPRV_RGB_MAT4_MTX_RB_SHIFT;
	val |= (rgb2rgb.matrix[2][1] & 0xfff) << ISPPRV_RGB_MAT4_MTX_GB_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT4);

	val = (rgb2rgb.matrix[2][2] & 0xfff) << ISPPRV_RGB_MAT5_MTX_BB_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT5);

	val = (rgb2rgb.offset[0] & 0x3ff) << ISPPRV_RGB_OFF1_MTX_OFFG_SHIFT;
	val |= (rgb2rgb.offset[1] & 0x3ff) << ISPPRV_RGB_OFF1_MTX_OFFR_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF1);

	val = (rgb2rgb.offset[2] & 0x3ff) << ISPPRV_RGB_OFF2_MTX_OFFB_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF2);
}
EXPORT_SYMBOL_GPL(isppreview_config_rgb_blending);

/**
 * Configures the RGB-YCbYCr conversion matrix
 * @prev_csc: Structure containing the RGB to YCbYCr matrix and the
 *            YCbCr offset.
 **/
void isppreview_config_rgb_to_ycbcr(struct ispprev_csc prev_csc)
{
	u32 val = 0;

	val = (prev_csc.matrix[0][0] & 0x3ff) << ISPPRV_CSC0_RY_SHIFT;
	val |= (prev_csc.matrix[0][1] & 0x3ff) << ISPPRV_CSC0_GY_SHIFT;
	val |= (prev_csc.matrix[0][2] & 0x3ff) << ISPPRV_CSC0_BY_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC0);

	val = (prev_csc.matrix[1][0] & 0x3ff) << ISPPRV_CSC1_RCB_SHIFT;
	val |= (prev_csc.matrix[1][1] & 0x3ff) << ISPPRV_CSC1_GCB_SHIFT;
	val |= (prev_csc.matrix[1][2] & 0x3ff) << ISPPRV_CSC1_BCB_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC1);

	val = (prev_csc.matrix[2][0] & 0x3ff) << ISPPRV_CSC2_RCR_SHIFT;
	val |= (prev_csc.matrix[2][1] & 0x3ff) << ISPPRV_CSC2_GCR_SHIFT;
	val |= (prev_csc.matrix[2][2] & 0x3ff) << ISPPRV_CSC2_BCR_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC2);

	val = (prev_csc.offset[0] & 0xff) << ISPPRV_CSC_OFFSET_CR_SHIFT;
	val |= (prev_csc.offset[1] & 0xff) << ISPPRV_CSC_OFFSET_CB_SHIFT;
	val |= (prev_csc.offset[2] & 0xff) << ISPPRV_CSC_OFFSET_Y_SHIFT;
	isp_reg_writel(val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC_OFFSET);
}
EXPORT_SYMBOL_GPL(isppreview_config_rgb_to_ycbcr);

/**
 * isppreview_query_contrast - Query the contrast.
 * @contrast: Pointer to hold the current programmed contrast value.
 **/
void isppreview_query_contrast(u8 *contrast)
{
	u32 brt_cnt_val = 0;

	brt_cnt_val = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
	*contrast = (brt_cnt_val >> ISPPRV_CNT_BRT_CNT_SHIFT) & 0xff;
	DPRINTK_ISPPREV(" Current brt cnt value in hw is %x\n", brt_cnt_val);
}
EXPORT_SYMBOL_GPL(isppreview_query_contrast);

/**
 * isppreview_update_contrast - Updates the contrast.
 * @contrast: Pointer to hold the current programmed contrast value.
 *
 * Value should be programmed before enabling the module.
 **/
void isppreview_update_contrast(u8 *contrast)
{
	ispprev_obj.contrast = *contrast;
}
EXPORT_SYMBOL_GPL(isppreview_update_contrast);

/**
 * isppreview_config_contrast - Configures the Contrast.
 * @contrast: 8 bit value in U8Q4 format.
 *
 * Value should be programmed before enabling the module.
 **/
void isppreview_config_contrast(u8 contrast)
{
	u32 brt_cnt_val = 0;

	brt_cnt_val = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
	brt_cnt_val &= ~(0xff << ISPPRV_CNT_BRT_CNT_SHIFT);
	contrast &= 0xff;
	isp_reg_writel(brt_cnt_val | contrast << ISPPRV_CNT_BRT_CNT_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
}
EXPORT_SYMBOL_GPL(isppreview_config_contrast);

/**
 * isppreview_get_contrast_range - Gets the range contrast value.
 * @min_contrast: Pointer to hold the minimum Contrast value.
 * @max_contrast: Pointer to hold the maximum Contrast value.
 **/
void isppreview_get_contrast_range(u8 *min_contrast, u8 *max_contrast)
{
	*min_contrast = ISPPRV_CONTRAST_MIN;
	*max_contrast = ISPPRV_CONTRAST_MAX;
}
EXPORT_SYMBOL_GPL(isppreview_get_contrast_range);

/**
 * isppreview_update_brightness - Updates the brightness in preview module.
 * @brightness: Pointer to hold the current programmed brightness value.
 *
 **/
void isppreview_update_brightness(u8 *brightness)
{
	ispprev_obj.brightness = *brightness;
}
EXPORT_SYMBOL_GPL(isppreview_update_brightness);

/**
 * isppreview_config_brightness - Configures the brightness.
 * @contrast: 8bitvalue in U8Q0 format.
 **/
void isppreview_config_brightness(u8 brightness)
{
	u32 brt_cnt_val = 0;

	DPRINTK_ISPPREV("\tConfiguring brightness in ISP: %d\n", brightness);
	brt_cnt_val = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
	brt_cnt_val &= ~(0xff << ISPPRV_CNT_BRT_BRT_SHIFT);
	brightness &= 0xff;
	isp_reg_writel(brt_cnt_val | brightness << ISPPRV_CNT_BRT_BRT_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
}
EXPORT_SYMBOL_GPL(isppreview_config_brightness);

/**
 * isppreview_query_brightness - Query the brightness.
 * @brightness: Pointer to hold the current programmed brightness value.
 **/
void isppreview_query_brightness(u8 *brightness)
{
	*brightness = isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT);
}
EXPORT_SYMBOL_GPL(isppreview_query_brightness);

/**
 * isppreview_get_brightness_range - Gets the range brightness value
 * @min_brightness: Pointer to hold the minimum brightness value
 * @max_brightness: Pointer to hold the maximum brightness value
 **/
void isppreview_get_brightness_range(u8 *min_brightness, u8 *max_brightness)
{
	*min_brightness = ISPPRV_BRIGHT_MIN;
	*max_brightness = ISPPRV_BRIGHT_MAX;
}
EXPORT_SYMBOL_GPL(isppreview_get_brightness_range);

/**
 * isppreview_set_color - Sets the color effect.
 * @mode: Indicates the required color effect.
 **/
void isppreview_set_color(u8 *mode)
{
	ispprev_obj.color = *mode;
	update_color_matrix = 1;
}
EXPORT_SYMBOL_GPL(isppreview_set_color);

/**
 * isppreview_get_color - Gets the current color effect.
 * @mode: Indicates the current color effect.
 **/
void isppreview_get_color(u8 *mode)
{
	*mode = ispprev_obj.color;
}
EXPORT_SYMBOL_GPL(isppreview_get_color);

/**
 * isppreview_config_yc_range - Configures the max and min Y and C values.
 * @yclimit: Structure containing the range of Y and C values.
 **/
void isppreview_config_yc_range(struct ispprev_yclimit yclimit)
{
	isp_reg_writel(yclimit.maxC << ISPPRV_SETUP_YC_MAXC_SHIFT |
		       yclimit.maxY << ISPPRV_SETUP_YC_MAXY_SHIFT |
		       yclimit.minC << ISPPRV_SETUP_YC_MINC_SHIFT |
		       yclimit.minY << ISPPRV_SETUP_YC_MINY_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SETUP_YC);
}
EXPORT_SYMBOL_GPL(isppreview_config_yc_range);

/**
 * isppreview_try_size - Calculates output dimensions with the modules enabled.
 * @input_w: input width for the preview in number of pixels per line
 * @input_h: input height for the preview in number of lines
 * @output_w: output width from the preview in number of pixels per line
 * @output_h: output height for the preview in number of lines
 *
 * Calculates the number of pixels cropped in the submodules that are enabled,
 * Fills up the output width height variables in the isp_prev structure.
 **/
int isppreview_try_size(u32 input_w, u32 input_h, u32 *output_w, u32 *output_h)
{
	u32 prevout_w = input_w;
	u32 prevout_h = input_h;
	u32 div = 0;
	int max_out;

	ispprev_obj.previn_w = input_w;
	ispprev_obj.previn_h = input_h;

	if (input_w < 32 || input_h < 32) {
		printk(KERN_ERR "ISP_ERR : preview does not support "
		       "width < 16 or height < 32 \n");
		return -EINVAL;
	}
	if (omap_rev() == OMAP3430_REV_ES1_0)
		max_out = ISPPRV_MAXOUTPUT_WIDTH;
	else
		max_out = ISPPRV_MAXOUTPUT_WIDTH_ES2;

	ispprev_obj.fmtavg = 0;

	if (input_w > max_out) {
		div = (input_w/max_out);
		if (div >= 2 && div < 4) {
			ispprev_obj.fmtavg = 1;
			prevout_w /= 2;
		} else if (div >= 4 && div < 8) {
			ispprev_obj.fmtavg = 2;
			prevout_w /= 4;
		} else if (div >= 8) {
			ispprev_obj.fmtavg = 3;
			prevout_w /= 8;
		}
	}

	if (ispprev_obj.hmed_en)
		prevout_w -= 4;
	if (ispprev_obj.nf_en) {
		prevout_w -= 4;
		prevout_h -= 4;
	}
	if (ispprev_obj.cfa_en) {
		switch (ispprev_obj.cfafmt) {
		case CFAFMT_BAYER:
		case CFAFMT_SONYVGA:
			prevout_w -= 4;
			prevout_h -= 4;
			break;
		case CFAFMT_RGBFOVEON:
		case CFAFMT_RRGGBBFOVEON:
		case CFAFMT_DNSPL:
		case CFAFMT_HONEYCOMB:
			prevout_h -= 2;
			break;
		};
	}
	if (ispprev_obj.yenh_en || ispprev_obj.csup_en)
		prevout_w -= 2;

	/* Start at the correct row/column by skipping
	 * a Sensor specific amount.
	 */
	prevout_w -= ispprev_obj.sph;
	prevout_h -= ispprev_obj.slv;


	if (prevout_w % 2)
		prevout_w -= 1;

	if (ispprev_obj.prev_outfmt == PREVIEW_MEM) {
		if (((prevout_w * 2) & ISP_32B_BOUNDARY_OFFSET) !=
		    (prevout_w * 2)) {
			prevout_w = ((prevout_w * 2) &
				     ISP_32B_BOUNDARY_OFFSET) / 2;
		}
	}
	*output_w = prevout_w;
	ispprev_obj.prevout_w = prevout_w;
	*output_h = prevout_h;
	ispprev_obj.prevout_h = prevout_h;
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_try_size);

/**
 * isppreview_config_size - Sets the size of ISP preview output.
 * @input_w: input width for the preview in number of pixels per line
 * @input_h: input height for the preview in number of lines
 * @output_w: output width from the preview in number of pixels per line
 * @output_h: output height for the preview in number of lines
 *
 * Configures the appropriate values stored in the isp_prev structure to
 * HORZ/VERT_INFO. Configures PRV_AVE if needed for downsampling as calculated
 * in trysize.
 **/
int isppreview_config_size(u32 input_w, u32 input_h, u32 output_w, u32 output_h)
{
	u32 prevsdroff;

	if ((output_w != ispprev_obj.prevout_w) ||
	    (output_h != ispprev_obj.prevout_h)) {
		printk(KERN_ERR "ISP_ERR : isppreview_try_size should "
		       "be called before config size\n");
		return -EINVAL;
	}

	isp_reg_writel((ispprev_obj.sph << ISPPRV_HORZ_INFO_SPH_SHIFT) |
		       (ispprev_obj.previn_w - 1),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_HORZ_INFO);
	isp_reg_writel((ispprev_obj.slv << ISPPRV_VERT_INFO_SLV_SHIFT) |
		       (ispprev_obj.previn_h - 2),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_VERT_INFO);

	if (ispprev_obj.cfafmt == CFAFMT_BAYER)
		isp_reg_writel(ISPPRV_AVE_EVENDIST_2 <<
			       ISPPRV_AVE_EVENDIST_SHIFT |
			       ISPPRV_AVE_ODDDIST_2 <<
			       ISPPRV_AVE_ODDDIST_SHIFT |
			       ispprev_obj.fmtavg,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_AVE);

	if (ispprev_obj.prev_outfmt == PREVIEW_MEM) {
		prevsdroff = ispprev_obj.prevout_w * 2;
		if ((prevsdroff & ISP_32B_BOUNDARY_OFFSET) != prevsdroff) {
			DPRINTK_ISPPREV("ISP_WARN: Preview output buffer line"
					" size is truncated"
					" to 32byte boundary\n");
			prevsdroff &= ISP_32B_BOUNDARY_BUF ;
		}
		isppreview_config_outlineoffset(prevsdroff);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_config_size);

/**
 * isppreview_config_inlineoffset - Configures the Read address line offset.
 * @offset: Line Offset for the input image.
 **/
int isppreview_config_inlineoffset(u32 offset)
{
	if ((offset & ISP_32B_BOUNDARY_OFFSET) == offset) {
		isp_reg_writel(offset & 0xffff,
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_RADR_OFFSET);
	} else {
		printk(KERN_ERR "ISP_ERR : Offset should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_config_inlineoffset);

/**
 * isppreview_set_inaddr - Sets memory address of input frame.
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Configures the memory address from which the input frame is to be read.
 **/
int isppreview_set_inaddr(u32 addr)
{
	if ((addr & ISP_32B_BOUNDARY_BUF) == addr)
		isp_reg_writel(addr, OMAP3_ISP_IOMEM_PREV, ISPPRV_RSDR_ADDR);
	else {
		printk(KERN_ERR "ISP_ERR: Address should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_set_inaddr);

/**
 * isppreview_config_outlineoffset - Configures the Write address line offset.
 * @offset: Line Offset for the preview output.
 **/
int isppreview_config_outlineoffset(u32 offset)
{
	if ((offset & ISP_32B_BOUNDARY_OFFSET) != offset) {
		printk(KERN_ERR "ISP_ERR : Offset should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	isp_reg_writel(offset & 0xffff, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_WADD_OFFSET);
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_config_outlineoffset);

/**
 * isppreview_set_outaddr - Sets the memory address to store output frame
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Configures the memory address to which the output frame is written.
 **/
int isppreview_set_outaddr(u32 addr)
{
	if ((addr & ISP_32B_BOUNDARY_BUF) != addr) {
		printk(KERN_ERR "ISP_ERR: Address should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	isp_reg_writel(addr, OMAP3_ISP_IOMEM_PREV, ISPPRV_WSDR_ADDR);
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_set_outaddr);

/**
 * isppreview_config_darklineoffset - Sets the Dark frame address line offset.
 * @offset: Line Offset for the Darkframe.
 **/
int isppreview_config_darklineoffset(u32 offset)
{
	if ((offset & ISP_32B_BOUNDARY_OFFSET) != offset) {
		printk(KERN_ERR "ISP_ERR : Offset should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	isp_reg_writel(offset & 0xffff, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_DRKF_OFFSET);
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_config_darklineoffset);

/**
 * isppreview_set_darkaddr - Sets the memory address to store Dark frame.
 * @addr: 32bit memory address aligned on 32 bit boundary.
 **/
int isppreview_set_darkaddr(u32 addr)
{
	if ((addr & ISP_32B_BOUNDARY_BUF) != addr) {
		printk(KERN_ERR "ISP_ERR : Address should be in 32 byte "
		       "boundary\n");
		return -EINVAL;
	}
	isp_reg_writel(addr, OMAP3_ISP_IOMEM_PREV, ISPPRV_DSDR_ADDR);
	return 0;
}
EXPORT_SYMBOL_GPL(isppreview_set_darkaddr);

void __isppreview_enable(int enable)
{
	if (enable)
		isp_reg_or(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_EN);
	else
		isp_reg_and(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ~ISPPRV_PCR_EN);
}

/**
 * isppreview_enable - Enables the Preview module.
 * @enable: 1 - Enables the preview module.
 *
 * Client should configure all the sub modules in Preview before this.
 **/
void isppreview_enable(int enable)
{
	__isppreview_enable(enable);
	ispprev_obj.pm_state = enable;
}
EXPORT_SYMBOL_GPL(isppreview_enable);

/**
 * isppreview_suspend - Suspend Preview module.
 **/
void isppreview_suspend(void)
{
	if (ispprev_obj.pm_state)
		__isppreview_enable(0);
}
EXPORT_SYMBOL_GPL(isppreview_suspend);

/**
 * isppreview_resume - Resume Preview module.
 **/
void isppreview_resume(void)
{
	if (ispprev_obj.pm_state)
		__isppreview_enable(1);
}
EXPORT_SYMBOL_GPL(isppreview_resume);


/**
 * isppreview_busy - Gets busy state of preview module.
 **/
int isppreview_busy(void)
{
	return isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR) &
		ISPPRV_PCR_BUSY;
}
EXPORT_SYMBOL_GPL(isppreview_busy);

/**
 * isppreview_get_config - Gets parameters of preview module.
 **/
struct prev_params *isppreview_get_config(void)
{
	return prev_config_params;
}
EXPORT_SYMBOL_GPL(isppreview_get_config);

/**
 * isppreview_save_context - Saves the values of the preview module registers.
 **/
void isppreview_save_context(void)
{
	DPRINTK_ISPPREV("Saving context\n");
	isp_save_context(ispprev_reg_list);
}
EXPORT_SYMBOL_GPL(isppreview_save_context);

/**
 * isppreview_restore_context - Restores the values of preview module registers
 **/
void isppreview_restore_context(void)
{
	DPRINTK_ISPPREV("Restoring context\n");
	isp_restore_context(ispprev_reg_list);
}
EXPORT_SYMBOL_GPL(isppreview_restore_context);

/**
 * isppreview_print_status - Prints the values of the Preview Module registers.
 *
 * Also prints other debug information stored in the preview moduel.
 **/
void isppreview_print_status(void)
{
	DPRINTK_ISPPREV("Module in use =%d\n", ispprev_obj.prev_inuse);
	DPRINTK_ISPPREV("Preview Input format =%d, Output Format =%d\n",
			ispprev_obj.prev_inpfmt,
			ispprev_obj.prev_outfmt);
	DPRINTK_ISPPREV("Accepted Preview Input (width = %d,Height = %d)\n",
			ispprev_obj.previn_w,
			ispprev_obj.previn_h);
	DPRINTK_ISPPREV("Accepted Preview Output (width = %d,Height = %d)\n",
			ispprev_obj.prevout_w,
			ispprev_obj.prevout_h);
	DPRINTK_ISPPREV("###ISP_CTRL in preview =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_MAIN, ISP_CTRL));
	DPRINTK_ISPPREV("###ISP_IRQ0ENABLE in preview =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE));
	DPRINTK_ISPPREV("###ISP_IRQ0STATUS in preview =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS));
	DPRINTK_ISPPREV("###PRV PCR =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR));
	DPRINTK_ISPPREV("###PRV HORZ_INFO =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_HORZ_INFO));
	DPRINTK_ISPPREV("###PRV VERT_INFO =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_VERT_INFO));
	DPRINTK_ISPPREV("###PRV WSDR_ADDR =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_WSDR_ADDR));
	DPRINTK_ISPPREV("###PRV WADD_OFFSET =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV,
				      ISPPRV_WADD_OFFSET));
	DPRINTK_ISPPREV("###PRV AVE =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_AVE));
	DPRINTK_ISPPREV("###PRV HMED =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_HMED));
	DPRINTK_ISPPREV("###PRV NF =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_NF));
	DPRINTK_ISPPREV("###PRV WB_DGAIN =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_WB_DGAIN));
	DPRINTK_ISPPREV("###PRV WBGAIN =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_WBGAIN));
	DPRINTK_ISPPREV("###PRV WBSEL =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_WBSEL));
	DPRINTK_ISPPREV("###PRV CFA =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CFA));
	DPRINTK_ISPPREV("###PRV BLKADJOFF =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_BLKADJOFF));
	DPRINTK_ISPPREV("###PRV RGB_MAT1 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT1));
	DPRINTK_ISPPREV("###PRV RGB_MAT2 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT2));
	DPRINTK_ISPPREV("###PRV RGB_MAT3 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT3));
	DPRINTK_ISPPREV("###PRV RGB_MAT4 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT4));
	DPRINTK_ISPPREV("###PRV RGB_MAT5 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT5));
	DPRINTK_ISPPREV("###PRV RGB_OFF1 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF1));
	DPRINTK_ISPPREV("###PRV RGB_OFF2 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF2));
	DPRINTK_ISPPREV("###PRV CSC0 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC0));
	DPRINTK_ISPPREV("###PRV CSC1 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC1));
	DPRINTK_ISPPREV("###PRV CSC2 =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC2));
	DPRINTK_ISPPREV("###PRV CSC_OFFSET =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC_OFFSET));
	DPRINTK_ISPPREV("###PRV CNT_BRT =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT));
	DPRINTK_ISPPREV("###PRV CSUP =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_CSUP));
	DPRINTK_ISPPREV("###PRV SETUP_YC =0x%x\n",
			isp_reg_readl(OMAP3_ISP_IOMEM_PREV, ISPPRV_SETUP_YC));
}
EXPORT_SYMBOL_GPL(isppreview_print_status);

/**
 * isp_preview_init - Module Initialization.
 **/
int __init isp_preview_init(void)
{
	int i = 0;

	prev_config_params = kmalloc(sizeof(*prev_config_params), GFP_KERNEL);
	if (!prev_config_params) {
		printk(KERN_ERR "Can't get memory for isp_preview params!\n");
		return -ENOMEM;
	}
	params = prev_config_params;

	ispprev_obj.prev_inuse = 0;
	mutex_init(&ispprev_obj.ispprev_mutex);

	/* Init values */
	ispprev_obj.sph = 2;
	ispprev_obj.slv = 0;
	ispprev_obj.color = V4L2_COLORFX_NONE;
	ispprev_obj.contrast = ISPPRV_CONTRAST_DEF;
	params->contrast = ISPPRV_CONTRAST_DEF;
	ispprev_obj.brightness = ISPPRV_BRIGHT_DEF;
	params->brightness = ISPPRV_BRIGHT_DEF;
	params->average = NO_AVE;
	params->lens_shading_shift = 0;
	params->pix_fmt = YCPOS_YCrYCb;
	params->cfa.cfafmt = CFAFMT_BAYER;
	params->cfa.cfa_table = cfa_coef_table;
	params->cfa.cfa_gradthrs_horz = FLR_CFA_GRADTHRS_HORZ;
	params->cfa.cfa_gradthrs_vert = FLR_CFA_GRADTHRS_VERT;
	params->csup.gain = FLR_CSUP_GAIN;
	params->csup.thres = FLR_CSUP_THRES;
	params->csup.hypf_en = 0;
	params->ytable = luma_enhance_table;
	params->nf.spread = FLR_NF_STRGTH;
	memcpy(params->nf.table, noise_filter_table, sizeof(params->nf.table));
	params->dcor.couplet_mode_en = 1;
	for (i = 0; i < 4; i++)
		params->dcor.detect_correct[i] = 0xE;
	params->gtable.bluetable = bluegamma_table;
	params->gtable.greentable = greengamma_table;
	params->gtable.redtable = redgamma_table;
	params->wbal.dgain = FLR_WBAL_DGAIN;
	if (omap_rev() > OMAP3430_REV_ES1_0) {
		params->wbal.coef0 = FLR_WBAL_COEF0_ES1;
		params->wbal.coef1 = FLR_WBAL_COEF1_ES1;
		params->wbal.coef2 = FLR_WBAL_COEF2_ES1;
		params->wbal.coef3 = FLR_WBAL_COEF3_ES1;
	} else {
		params->wbal.coef0 = FLR_WBAL_COEF0;
		params->wbal.coef1 = FLR_WBAL_COEF1;
		params->wbal.coef2 = FLR_WBAL_COEF2;
		params->wbal.coef3 = FLR_WBAL_COEF3;
	}
	params->blk_adj.red = FLR_BLKADJ_RED;
	params->blk_adj.green = FLR_BLKADJ_GREEN;
	params->blk_adj.blue = FLR_BLKADJ_BLUE;
	params->rgb2rgb = flr_rgb2rgb;
	params->rgb2ycbcr = flr_prev_csc[ispprev_obj.color];

	params->features = PREV_CFA | PREV_DEFECT_COR | PREV_NOISE_FILTER;
	params->features &= ~(PREV_AVERAGER | PREV_INVERSE_ALAW |
			      PREV_HORZ_MEDIAN_FILTER |
			      PREV_GAMMA_BYPASS |
			      PREV_DARK_FRAME_SUBTRACT |
			      PREV_LENS_SHADING |
			      PREV_DARK_FRAME_CAPTURE |
			      PREV_CHROMA_SUPPRESS |
			      PREV_LUMA_ENHANCE);
	return 0;
}

/**
 * isp_preview_cleanup - Module Cleanup.
 **/
void isp_preview_cleanup(void)
{
	kfree(prev_config_params);
}
