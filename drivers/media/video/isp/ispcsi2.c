/*
 * ispcsi2.c
 *
 * Driver Library for ISP CSI Control module in TI's OMAP3 Camera ISP
 * ISP CSI interface and IRQ related APIs are defined here.
 *
 * Copyright (C) 2009 Texas Instruments.
 *
 * Contributors:
 * 	Sergio Aguirre <saaguirre@ti.com>
 * 	Dominic Curran <dcurran@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/delay.h>
#include <media/v4l2-common.h>

#include "isp.h"
#include "ispreg.h"
#include "ispcsi2.h"

static struct isp_csi2_cfg current_csi2_cfg;
static struct isp_csi2_cfg_update current_csi2_cfg_update;

static bool update_complexio_cfg1;
static bool update_phy_cfg0;
static bool update_phy_cfg1;
static bool update_ctx_ctrl1[8];
static bool update_ctx_ctrl2[8];
static bool update_ctx_ctrl3[8];
static bool update_timing;
static bool update_ctrl;
static bool uses_videoport;

/**
 * isp_csi2_complexio_lanes_config - Configuration of CSI2 ComplexIO lanes.
 * @reqcfg: Pointer to structure containing desired lane configuration
 *
 * Validates and saves to internal driver memory the passed configuration.
 * Returns 0 if successful, or -EINVAL if null pointer is passed, invalid
 * lane position or polarity is set, and if 2 lanes try to occupy the same
 * position. To apply this settings, use the isp_csi2_complexio_lanes_update()
 * function just after calling this function.
 **/
int isp_csi2_complexio_lanes_config(struct isp_csi2_lanes_cfg *reqcfg)
{
	int i;
	bool pos_occupied[5] = {false, false, false, false, false};
	struct isp_csi2_lanes_cfg *currlanes = &current_csi2_cfg.lanes;
	struct isp_csi2_lanes_cfg_update *currlanes_u =
		&current_csi2_cfg_update.lanes;

	/* Validating parameters sent by driver */
	if (reqcfg == NULL) {
		printk(KERN_ERR "Invalid Complex IO Configuration sent by"
		       " sensor\n");
		goto err_einval;
	}

	/* Data lanes verification */
	for (i = 0; i < 4; i++) {
		if ((reqcfg->data[i].pol > 1) || (reqcfg->data[i].pos > 5)) {
			printk(KERN_ERR "Invalid CSI-2 Complex IO configuration"
			       " parameters for data lane #%d\n", i);
			goto err_einval;
		}
		if (pos_occupied[reqcfg->data[i].pos - 1] &&
		    reqcfg->data[i].pos > 0) {
			printk(KERN_ERR "Lane #%d already occupied\n",
			       reqcfg->data[i].pos);
			goto err_einval;
		} else
			pos_occupied[reqcfg->data[i].pos - 1] = true;
	}

	/* Clock lane verification */
	if ((reqcfg->clk.pol > 1) || (reqcfg->clk.pos > 5) ||
	    (reqcfg->clk.pos == 0)) {
		printk(KERN_ERR "Invalid CSI-2 Complex IO configuration"
		       " parameters for clock lane\n");
		goto err_einval;
	}
	if (pos_occupied[reqcfg->clk.pos - 1]) {
		printk(KERN_ERR "Lane #%d already occupied",
		       reqcfg->clk.pos);
		goto err_einval;
	} else
		pos_occupied[reqcfg->clk.pos - 1] = true;

	for (i = 0; i < 4; i++) {
		if (currlanes->data[i].pos != reqcfg->data[i].pos) {
			currlanes->data[i].pos = reqcfg->data[i].pos;
			currlanes_u->data[i] = true;
			update_complexio_cfg1 = true;
		}
		if (currlanes->data[i].pol != reqcfg->data[i].pol) {
			currlanes->data[i].pol = reqcfg->data[i].pol;
			currlanes_u->data[i] = true;
			update_complexio_cfg1 = true;
		}
	}

	if (currlanes->clk.pos != reqcfg->clk.pos) {
		currlanes->clk.pos = reqcfg->clk.pos;
		currlanes_u->clk = true;
		update_complexio_cfg1 = true;
	}
	if (currlanes->clk.pol != reqcfg->clk.pol) {
		currlanes->clk.pol = reqcfg->clk.pol;
		currlanes_u->clk = true;
		update_complexio_cfg1 = true;
	}
	return 0;
err_einval:
	return -EINVAL;
}

/**
 * isp_csi2_complexio_lanes_update - Applies CSI2 ComplexIO lanes configuration.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_complexio_lanes_config() function.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_complexio_lanes_config() function, unless the force_update flag is
 * set to true.
 * Always returns 0.
 **/
int isp_csi2_complexio_lanes_update(bool force_update)
{
	struct isp_csi2_lanes_cfg *currlanes = &current_csi2_cfg.lanes;
	struct isp_csi2_lanes_cfg_update *currlanes_u =
		&current_csi2_cfg_update.lanes;
	u32 reg;
	int i;

	if (!update_complexio_cfg1 && !force_update)
		return 0;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);
	for (i = 0; i < 4; i++) {
		if (currlanes_u->data[i] || force_update) {
			reg &= ~(ISPCSI2_COMPLEXIO_CFG1_DATA_POL_MASK(i + 1) |
				 ISPCSI2_COMPLEXIO_CFG1_DATA_POSITION_MASK(i +
									   1));
			reg |= (currlanes->data[i].pol <<
				ISPCSI2_COMPLEXIO_CFG1_DATA_POL_SHIFT(i + 1));
			reg |= (currlanes->data[i].pos <<
				ISPCSI2_COMPLEXIO_CFG1_DATA_POSITION_SHIFT(i +
									   1));
			currlanes_u->data[i] = false;
		}
	}

	if (currlanes_u->clk || force_update) {
		reg &= ~(ISPCSI2_COMPLEXIO_CFG1_CLOCK_POL_MASK |
			 ISPCSI2_COMPLEXIO_CFG1_CLOCK_POSITION_MASK);
		reg |= (currlanes->clk.pol <<
			ISPCSI2_COMPLEXIO_CFG1_CLOCK_POL_SHIFT);
		reg |= (currlanes->clk.pos <<
			ISPCSI2_COMPLEXIO_CFG1_CLOCK_POSITION_SHIFT);
		currlanes_u->clk = false;
	}
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);

	update_complexio_cfg1 = false;
	return 0;
}

/**
 * isp_csi2_complexio_lanes_get - Gets CSI2 ComplexIO lanes configuration.
 *
 * Gets settings from HW registers and fills in the internal driver memory
 * Always returns 0.
 **/
int isp_csi2_complexio_lanes_get(void)
{
	struct isp_csi2_lanes_cfg *currlanes = &current_csi2_cfg.lanes;
	struct isp_csi2_lanes_cfg_update *currlanes_u =
		&current_csi2_cfg_update.lanes;
	u32 reg;
	int i;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);
	for (i = 0; i < 4; i++) {
		currlanes->data[i].pol = (reg &
					  ISPCSI2_COMPLEXIO_CFG1_DATA_POL_MASK(i + 1)) >>
			ISPCSI2_COMPLEXIO_CFG1_DATA_POL_SHIFT(i + 1);
		currlanes->data[i].pos = (reg &
					  ISPCSI2_COMPLEXIO_CFG1_DATA_POSITION_MASK(i + 1)) >>
			ISPCSI2_COMPLEXIO_CFG1_DATA_POSITION_SHIFT(i + 1);
		currlanes_u->data[i] = false;
	}
	currlanes->clk.pol = (reg & ISPCSI2_COMPLEXIO_CFG1_CLOCK_POL_MASK) >>
		ISPCSI2_COMPLEXIO_CFG1_CLOCK_POL_SHIFT;
	currlanes->clk.pos = (reg &
			      ISPCSI2_COMPLEXIO_CFG1_CLOCK_POSITION_MASK) >>
		ISPCSI2_COMPLEXIO_CFG1_CLOCK_POSITION_SHIFT;
	currlanes_u->clk = false;

	update_complexio_cfg1 = false;
	return 0;
}

/**
 * isp_csi2_complexio_power_status - Gets CSI2 ComplexIO power status.
 *
 * Returns 3 possible valid states: ISP_CSI2_POWER_OFF, ISP_CSI2_POWER_ON,
 * and ISP_CSI2_POWER_ULPW.
 **/
static enum isp_csi2_power_cmds isp_csi2_complexio_power_status(void)
{
	enum isp_csi2_power_cmds ret;
	u32 reg;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1) &
		ISPCSI2_COMPLEXIO_CFG1_PWR_STATUS_MASK;
	switch (reg) {
	case ISPCSI2_COMPLEXIO_CFG1_PWR_STATUS_OFF:
		ret = ISP_CSI2_POWER_OFF;
		break;
	case ISPCSI2_COMPLEXIO_CFG1_PWR_STATUS_ON:
		ret = ISP_CSI2_POWER_ON;
		break;
	case ISPCSI2_COMPLEXIO_CFG1_PWR_STATUS_ULPW:
		ret = ISP_CSI2_POWER_ULPW;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/**
 * isp_csi2_complexio_power_autoswitch - Sets CSI2 ComplexIO power autoswitch.
 * @enable: Sets or clears the autoswitch function enable flag.
 *
 * Always returns 0.
 **/
int isp_csi2_complexio_power_autoswitch(bool enable)
{
	u32 reg;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);
	reg &= ~ISPCSI2_COMPLEXIO_CFG1_PWR_AUTO_MASK;

	if (enable)
		reg |= ISPCSI2_COMPLEXIO_CFG1_PWR_AUTO_ENABLE;
	else
		reg |= ISPCSI2_COMPLEXIO_CFG1_PWR_AUTO_DISABLE;

	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);
	return 0;
}

/**
 * isp_csi2_complexio_power - Sets the desired power command for CSI2 ComplexIO.
 * @power_cmd: Power command to be set.
 *
 * Returns 0 if successful, or -EBUSY if the retry count is exceeded.
 **/
int isp_csi2_complexio_power(enum isp_csi2_power_cmds power_cmd)
{
	enum isp_csi2_power_cmds current_state;
	u32 reg;
	u8 retry_count;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1) &
		~ISPCSI2_COMPLEXIO_CFG1_PWR_CMD_MASK;
	switch (power_cmd) {
	case ISP_CSI2_POWER_OFF:
		reg |= ISPCSI2_COMPLEXIO_CFG1_PWR_CMD_OFF;
		break;
	case ISP_CSI2_POWER_ON:
		reg |= ISPCSI2_COMPLEXIO_CFG1_PWR_CMD_ON;
		break;
	case ISP_CSI2_POWER_ULPW:
		reg |= ISPCSI2_COMPLEXIO_CFG1_PWR_CMD_ULPW;
		break;
	default:
		printk(KERN_ERR "CSI2: ERROR - Wrong Power command!\n");
		return -EINVAL;
	}
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_COMPLEXIO_CFG1);

	retry_count = 0;
	do {
		udelay(50);
		current_state = isp_csi2_complexio_power_status();

		if (current_state != power_cmd) {
			printk(KERN_DEBUG "CSI2: Complex IO power command not"
			       " yet taken.");
			if (++retry_count < 100) {
				printk(KERN_DEBUG " Retrying...\n");
				udelay(50);
			} else {
				printk(KERN_DEBUG " Retry count exceeded!\n");
			}
		}
	} while ((current_state != power_cmd) && (retry_count < 100));

	if (retry_count == 100)
		return -EBUSY;

	return 0;
}

/**
 * isp_csi2_ctrl_config_frame_mode - Configure if_en behaviour for CSI2
 * @frame_mode: Desired action for IF_EN switch off. 0 - disable IF immediately
 *              1 - disable after all Frame end Code is received in all
 *              contexts.
 *
 * Validates and saves to internal driver memory the passed configuration.
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_frame_mode(enum isp_csi2_frame_mode frame_mode)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->frame_mode != frame_mode) {
		currctrl->frame_mode = frame_mode;
		currctrl_u->frame_mode = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_vp_clk_enable - Enables/disables CSI2 Videoport clock.
 * @vp_clk_enable: Boolean value to specify the Videoport clock state.
 *
 * Validates and saves to internal driver memory the passed configuration.
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_vp_clk_enable(bool vp_clk_enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->vp_clk_enable != vp_clk_enable) {
		currctrl->vp_clk_enable = vp_clk_enable;
		currctrl_u->vp_clk_enable = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_vp_only_enable - Sets CSI2 Videoport clock as exclusive
 * @vp_only_enable: Boolean value to specify if the Videoport clock is
 *                  exclusive, setting the OCP port as disabled.
 *
 * Validates and saves to internal driver memory the passed configuration.
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_vp_only_enable(bool vp_only_enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->vp_only_enable != vp_only_enable) {
		currctrl->vp_only_enable = vp_only_enable;
		currctrl_u->vp_only_enable = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_vp_out_ctrl - Sets CSI2 Videoport clock divider
 * @vp_out_ctrl: Divider value for setting videoport clock frequency based on
 *               OCP port frequency, valid dividers are between 1 and 4.
 *
 * Validates and saves to internal driver memory the passed configuration.
 * Returns 0 if successful, or -EINVAL if wrong divider value is passed.
 **/
int isp_csi2_ctrl_config_vp_out_ctrl(u8 vp_out_ctrl)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if ((vp_out_ctrl == 0) || (vp_out_ctrl > 4)) {
		printk(KERN_ERR "CSI2: Wrong divisor value. Must be between"
		       " 1 and 4");
		return -EINVAL;
	}

	if (currctrl->vp_out_ctrl != vp_out_ctrl) {
		currctrl->vp_out_ctrl = vp_out_ctrl;
		currctrl_u->vp_out_ctrl = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_debug_enable - Sets CSI2 debug
 * @debug_enable: Boolean for setting debug configuration on CSI2.
 *
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_debug_enable(bool debug_enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->debug_enable != debug_enable) {
		currctrl->debug_enable = debug_enable;
		currctrl_u->debug_enable = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_burst_size - Sets CSI2 burst size.
 * @burst_size: Burst size of the memory saving capability of receiver.
 *
 * Returns 0 if successful, or -EINVAL if burst size is wrong.
 **/
int isp_csi2_ctrl_config_burst_size(u8 burst_size)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;
	if (burst_size > 3) {
		printk(KERN_ERR "CSI2: Wrong burst size. Must be between"
		       " 0 and 3");
		return -EINVAL;
	}

	if (currctrl->burst_size != burst_size) {
		currctrl->burst_size = burst_size;
		currctrl_u->burst_size = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_ecc_enable - Enables ECC on CSI2 Receiver
 * @ecc_enable: Boolean to enable/disable the CSI2 receiver ECC handling.
 *
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_ecc_enable(bool ecc_enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->ecc_enable != ecc_enable) {
		currctrl->ecc_enable = ecc_enable;
		currctrl_u->ecc_enable = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_ecc_enable - Enables ECC on CSI2 Receiver
 * @ecc_enable: Boolean to enable/disable the CSI2 receiver ECC handling.
 *
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_secure_mode(bool secure_mode)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->secure_mode != secure_mode) {
		currctrl->secure_mode = secure_mode;
		currctrl_u->secure_mode = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_config_if_enable - Enables CSI2 Receiver interface.
 * @if_enable: Boolean to enable/disable the CSI2 receiver interface.
 *
 * Always returns 0.
 **/
int isp_csi2_ctrl_config_if_enable(bool if_enable)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;

	if (currctrl->if_enable != if_enable) {
		currctrl->if_enable = if_enable;
		currctrl_u->if_enable = true;
		update_ctrl = true;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_update - Applies CSI2 control configuration.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_ctrl_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_ctrl_config_*() functions, unless the force_update flag is
 * set to true.
 * Always returns 0.
 **/
int isp_csi2_ctrl_update(bool force_update)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;
	u32 reg;

	if (update_ctrl || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTRL);
		if (currctrl_u->frame_mode || force_update) {
			reg &= ~ISPCSI2_CTRL_FRAME_MASK;
			if (currctrl->frame_mode)
				reg |= ISPCSI2_CTRL_FRAME_DISABLE_FEC;
			else
				reg |= ISPCSI2_CTRL_FRAME_DISABLE_IMM;
			currctrl_u->frame_mode = false;
		}
		if (currctrl_u->vp_clk_enable || force_update) {
			reg &= ~ISPCSI2_CTRL_VP_CLK_EN_MASK;
			if (currctrl->vp_clk_enable)
				reg |= ISPCSI2_CTRL_VP_CLK_EN_ENABLE;
			else
				reg |= ISPCSI2_CTRL_VP_CLK_EN_DISABLE;
			currctrl_u->vp_clk_enable = false;
		}
		if (currctrl_u->vp_only_enable || force_update) {
			reg &= ~ISPCSI2_CTRL_VP_ONLY_EN_MASK;
			uses_videoport = currctrl->vp_only_enable;
			if (currctrl->vp_only_enable)
				reg |= ISPCSI2_CTRL_VP_ONLY_EN_ENABLE;
			else
				reg |= ISPCSI2_CTRL_VP_ONLY_EN_DISABLE;
			currctrl_u->vp_only_enable = false;
		}
		if (currctrl_u->vp_out_ctrl || force_update) {
			reg &= ~ISPCSI2_CTRL_VP_OUT_CTRL_MASK;
			reg |= (currctrl->vp_out_ctrl - 1) <<
				ISPCSI2_CTRL_VP_OUT_CTRL_SHIFT;
			currctrl_u->vp_out_ctrl = false;
		}
		if (currctrl_u->debug_enable || force_update) {
			reg &= ~ISPCSI2_CTRL_DBG_EN_MASK;
			if (currctrl->debug_enable)
				reg |= ISPCSI2_CTRL_DBG_EN_ENABLE;
			else
				reg |= ISPCSI2_CTRL_DBG_EN_DISABLE;
			currctrl_u->debug_enable = false;
		}
		if (currctrl_u->burst_size || force_update) {
			reg &= ~ISPCSI2_CTRL_BURST_SIZE_MASK;
			reg |= currctrl->burst_size <<
				ISPCSI2_CTRL_BURST_SIZE_SHIFT;
			currctrl_u->burst_size = false;
		}
		if (currctrl_u->ecc_enable || force_update) {
			reg &= ~ISPCSI2_CTRL_ECC_EN_MASK;
			if (currctrl->ecc_enable)
				reg |= ISPCSI2_CTRL_ECC_EN_ENABLE;
			else
				reg |= ISPCSI2_CTRL_ECC_EN_DISABLE;
			currctrl_u->ecc_enable = false;
		}
		if (currctrl_u->secure_mode || force_update) {
			reg &= ~ISPCSI2_CTRL_SECURE_MASK;
			if (currctrl->secure_mode)
				reg |= ISPCSI2_CTRL_SECURE_ENABLE;
			else
				reg |= ISPCSI2_CTRL_SECURE_DISABLE;
			currctrl_u->secure_mode = false;
		}
		if (currctrl_u->if_enable || force_update) {
			reg &= ~ISPCSI2_CTRL_IF_EN_MASK;
			if (currctrl->if_enable)
				reg |= ISPCSI2_CTRL_IF_EN_ENABLE;
			else
				reg |= ISPCSI2_CTRL_IF_EN_DISABLE;
			currctrl_u->if_enable = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTRL);
		update_ctrl = false;
	}
	return 0;
}

/**
 * isp_csi2_ctrl_get - Gets CSI2 control configuration
 *
 * Always returns 0.
 **/
int isp_csi2_ctrl_get(void)
{
	struct isp_csi2_ctrl_cfg *currctrl = &current_csi2_cfg.ctrl;
	struct isp_csi2_ctrl_cfg_update *currctrl_u =
		&current_csi2_cfg_update.ctrl;
	u32 reg;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTRL);
	currctrl->frame_mode = (reg & ISPCSI2_CTRL_FRAME_MASK) >>
		ISPCSI2_CTRL_FRAME_SHIFT;
	currctrl_u->frame_mode = false;

	if ((reg & ISPCSI2_CTRL_VP_CLK_EN_MASK) ==
	    ISPCSI2_CTRL_VP_CLK_EN_ENABLE)
		currctrl->vp_clk_enable = true;
	else
		currctrl->vp_clk_enable = false;
	currctrl_u->vp_clk_enable = false;

	if ((reg & ISPCSI2_CTRL_VP_ONLY_EN_MASK) ==
	    ISPCSI2_CTRL_VP_ONLY_EN_ENABLE)
		currctrl->vp_only_enable = true;
	else
		currctrl->vp_only_enable = false;
	uses_videoport = currctrl->vp_only_enable;
	currctrl_u->vp_only_enable = false;

	currctrl->vp_out_ctrl = ((reg & ISPCSI2_CTRL_VP_OUT_CTRL_MASK) >>
				 ISPCSI2_CTRL_VP_OUT_CTRL_SHIFT) + 1;
	currctrl_u->vp_out_ctrl = false;

	if ((reg & ISPCSI2_CTRL_DBG_EN_MASK) == ISPCSI2_CTRL_DBG_EN_ENABLE)
		currctrl->debug_enable = true;
	else
		currctrl->debug_enable = false;
	currctrl_u->debug_enable = false;

	currctrl->burst_size = (reg & ISPCSI2_CTRL_BURST_SIZE_MASK) >>
		ISPCSI2_CTRL_BURST_SIZE_SHIFT;
	currctrl_u->burst_size = false;

	if ((reg & ISPCSI2_CTRL_ECC_EN_MASK) == ISPCSI2_CTRL_ECC_EN_ENABLE)
		currctrl->ecc_enable = true;
	else
		currctrl->ecc_enable = false;
	currctrl_u->ecc_enable = false;

	if ((reg & ISPCSI2_CTRL_SECURE_MASK) == ISPCSI2_CTRL_SECURE_ENABLE)
		currctrl->secure_mode = true;
	else
		currctrl->secure_mode = false;
	currctrl_u->secure_mode = false;

	if ((reg & ISPCSI2_CTRL_IF_EN_MASK) == ISPCSI2_CTRL_IF_EN_ENABLE)
		currctrl->if_enable = true;
	else
		currctrl->if_enable = false;
	currctrl_u->if_enable = false;

	update_ctrl = false;
	return 0;
}

/**
 * isp_csi2_ctx_validate - Validates the context number value
 * @ctxnum: Pointer to variable containing context number.
 *
 * If the value is not in range (3 bits), it is being ANDed with 0x7 to force
 * it to be on range.
 **/
static void isp_csi2_ctx_validate(u8 *ctxnum)
{
	if (*ctxnum > 7) {
		printk(KERN_ERR "Invalid context number. Forcing valid"
		       " value...\n");
		*ctxnum &= ~(0x7);
	}
}

/**
 * isp_csi2_ctx_config_virtual_id - Maps a virtual ID with a CSI2 Rx context
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @virtual_id: CSI2 Virtual ID to associate with specified context number.
 *
 * Returns 0 if successful, or -EINVAL if Virtual ID is not in range (0-3).
 **/
int isp_csi2_ctx_config_virtual_id(u8 ctxnum, u8 virtual_id)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	if (virtual_id > 3) {
		printk(KERN_ERR "Wrong requested virtual_id\n");
		return -EINVAL;
	}

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->virtual_id != virtual_id) {
		selected_ctx->virtual_id = virtual_id;
		selected_ctx_u->virtual_id = true;
		update_ctx_ctrl2[ctxnum] = true;
	}

	return 0;
}

/**
 * isp_csi2_ctx_config_frame_count - Sets frame count to be received in CSI2 Rx.
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @frame_count: Number of frames to acquire.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_frame_count(u8 ctxnum, u8 frame_count)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->frame_count != frame_count) {
		selected_ctx->frame_count = frame_count;
		selected_ctx_u->frame_count = true;
		update_ctx_ctrl1[ctxnum] = true;
	}

	return 0;
}

/**
 * isp_csi2_ctx_config_format - Maps a pixel format to a specified context.
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @pixformat: V4L2 structure for pixel format.
 *
 * Returns 0 if successful, or -EINVAL if the format is not supported by the
 * receiver.
 **/
int isp_csi2_ctx_config_format(u8 ctxnum, u32 pixformat)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;
	struct v4l2_pix_format pix;

	isp_csi2_ctx_validate(&ctxnum);

	pix.pixelformat = pixformat;
	switch (pix.pixelformat) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_RGB555:
	case V4L2_PIX_FMT_RGB555X:
	case V4L2_PIX_FMT_SGRBG10:
		break;
	default:
		printk(KERN_ERR "Context config pixel format unsupported\n");
		return -EINVAL;
	}

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	selected_ctx->format = pix;
	selected_ctx_u->format = true;
	update_ctx_ctrl2[ctxnum] = true;

	return 0;
}

/**
 * isp_csi2_ctx_config_alpha - Sets the alpha value for pixel format
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @alpha: Alpha value.
 *
 * Returns 0 if successful, or -EINVAL if the alpha value is bigger than 16383.
 **/
int isp_csi2_ctx_config_alpha(u8 ctxnum, u16 alpha)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	if (alpha > 0x3FFF) {
		printk(KERN_ERR "Wrong alpha value\n");
		return -EINVAL;
	}

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->alpha != alpha) {
		selected_ctx->alpha = alpha;
		selected_ctx_u->alpha = true;
		update_ctx_ctrl3[ctxnum] = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_data_offset - Sets the offset between received lines
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @data_offset: Offset between first pixel of each 2 contiguous lines.
 *
 * Returns 0 if successful, or -EINVAL if the line offset is bigger than 1023.
 **/
int isp_csi2_ctx_config_data_offset(u8 ctxnum, u16 data_offset)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	if (data_offset > 0x3FF) {
		printk(KERN_ERR "Wrong line offset\n");
		return -EINVAL;
	}

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->data_offset != data_offset) {
		selected_ctx->data_offset = data_offset;
		selected_ctx_u->data_offset = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_ping_addr - Sets Ping address for CSI2 Rx. buffer saving
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @ping_addr: 32 bit ISP MMU mapped address.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_ping_addr(u8 ctxnum, u32 ping_addr)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	ping_addr &= ~(0x1F);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->ping_addr != ping_addr) {
		selected_ctx->ping_addr = ping_addr;
		selected_ctx_u->ping_addr = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_pong_addr - Sets Pong address for CSI2 Rx. buffer saving
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @pong_addr: 32 bit ISP MMU mapped address.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_pong_addr(u8 ctxnum, u32 pong_addr)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	pong_addr &= ~(0x1F);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->pong_addr != pong_addr) {
		selected_ctx->pong_addr = pong_addr;
		selected_ctx_u->pong_addr = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_eof_enabled - Enables EOF signal assertion
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @eof_enabled: Boolean to enable/disable EOF signal assertion on received
 *               packets.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_eof_enabled(u8 ctxnum, bool eof_enabled)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->eof_enabled != eof_enabled) {
		selected_ctx->eof_enabled = eof_enabled;
		selected_ctx_u->eof_enabled = true;
		update_ctx_ctrl1[ctxnum] = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_eol_enabled - Enables EOL signal assertion
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @eol_enabled: Boolean to enable/disable EOL signal assertion on received
 *               packets.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_eol_enabled(u8 ctxnum, bool eol_enabled)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->eol_enabled != eol_enabled) {
		selected_ctx->eol_enabled = eol_enabled;
		selected_ctx_u->eol_enabled = true;
		update_ctx_ctrl1[ctxnum] = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_checksum_enabled - Enables Checksum check in rcvd packets
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @checksum_enabled: Boolean to enable/disable Checksum check on received
 *                    packets
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_checksum_enabled(u8 ctxnum, bool checksum_enabled)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->checksum_enabled != checksum_enabled) {
		selected_ctx->checksum_enabled = checksum_enabled;
		selected_ctx_u->checksum_enabled = true;
		update_ctx_ctrl1[ctxnum] = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_config_enabled - Enables specified CSI2 context
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @enabled: Boolean to enable/disable specified context.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_config_enabled(u8 ctxnum, bool enabled)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (selected_ctx->enabled != enabled) {
		selected_ctx->enabled = enabled;
		selected_ctx_u->enabled = true;
		update_ctx_ctrl1[ctxnum] = true;
	}
	return 0;
}

/**
 * isp_csi2_ctx_update - Applies CSI2 context configuration.
 * @ctxnum: Context number, valid between 0 and 7 values.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_ctx_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_ctx_config_*() functions, unless the force_update flag is
 * set to true.
 * Always returns 0.
 **/
int isp_csi2_ctx_update(u8 ctxnum, bool force_update)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;
	u32 reg;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	if (update_ctx_ctrl1[ctxnum] || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				    ISPCSI2_CTX_CTRL1(ctxnum));
		if (selected_ctx_u->frame_count || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL1_COUNT_MASK);
			reg |= selected_ctx->frame_count <<
				ISPCSI2_CTX_CTRL1_COUNT_SHIFT;
			selected_ctx_u->frame_count = false;
		}
		if (selected_ctx_u->eof_enabled || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL1_EOF_EN_MASK);
			if (selected_ctx->eof_enabled)
				reg |= ISPCSI2_CTX_CTRL1_EOF_EN_ENABLE;
			else
				reg |= ISPCSI2_CTX_CTRL1_EOF_EN_DISABLE;
			selected_ctx_u->eof_enabled = false;
		}
		if (selected_ctx_u->eol_enabled || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL1_EOL_EN_MASK);
			if (selected_ctx->eol_enabled)
				reg |= ISPCSI2_CTX_CTRL1_EOL_EN_ENABLE;
			else
				reg |= ISPCSI2_CTX_CTRL1_EOL_EN_DISABLE;
			selected_ctx_u->eol_enabled = false;
		}
		if (selected_ctx_u->checksum_enabled || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL1_CS_EN_MASK);
			if (selected_ctx->checksum_enabled)
				reg |= ISPCSI2_CTX_CTRL1_CS_EN_ENABLE;
			else
				reg |= ISPCSI2_CTX_CTRL1_CS_EN_DISABLE;
			selected_ctx_u->checksum_enabled = false;
		}
		if (selected_ctx_u->enabled || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL1_CTX_EN_MASK);
			if (selected_ctx->enabled)
				reg |= ISPCSI2_CTX_CTRL1_CTX_EN_ENABLE;
			else
				reg |= ISPCSI2_CTX_CTRL1_CTX_EN_DISABLE;
			selected_ctx_u->enabled = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_CTRL1(ctxnum));
		update_ctx_ctrl1[ctxnum] = false;
	}

	if (update_ctx_ctrl2[ctxnum] || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				    ISPCSI2_CTX_CTRL2(ctxnum));
		if (selected_ctx_u->virtual_id || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL2_VIRTUAL_ID_MASK);
			reg |= selected_ctx->virtual_id <<
				ISPCSI2_CTX_CTRL2_VIRTUAL_ID_SHIFT;
			selected_ctx_u->virtual_id = false;
		}

		if (selected_ctx_u->format || force_update) {
			struct v4l2_pix_format *pix;
			u16 new_format = 0;

			reg &= ~(ISPCSI2_CTX_CTRL2_FORMAT_MASK);
			pix = &selected_ctx->format;
			switch (pix->pixelformat) {
			case V4L2_PIX_FMT_RGB565:
			case V4L2_PIX_FMT_RGB565X:
				new_format = 0x22;
				break;
			case V4L2_PIX_FMT_YUYV:
			case V4L2_PIX_FMT_UYVY:
				if (uses_videoport)
					new_format = 0x9E;
				else
					new_format = 0x1E;
				break;
			case V4L2_PIX_FMT_RGB555:
			case V4L2_PIX_FMT_RGB555X:
				new_format = 0xA1;
				break;
			case V4L2_PIX_FMT_SGRBG10:
				if (uses_videoport)
					new_format = 0x12F;
				else
					new_format = 0xAB;
				break;
			}
			reg |= (new_format << ISPCSI2_CTX_CTRL2_FORMAT_SHIFT);
			selected_ctx_u->format = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_CTRL2(ctxnum));
		update_ctx_ctrl2[ctxnum] = false;
	}

	if (update_ctx_ctrl3[ctxnum] || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				    ISPCSI2_CTX_CTRL3(ctxnum));
		if (selected_ctx_u->alpha || force_update) {
			reg &= ~(ISPCSI2_CTX_CTRL3_ALPHA_MASK);
			reg |= (selected_ctx->alpha <<
				ISPCSI2_CTX_CTRL3_ALPHA_SHIFT);
			selected_ctx_u->alpha = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_CTRL3(ctxnum));
		update_ctx_ctrl3[ctxnum] = false;
	}

	if (selected_ctx_u->data_offset) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				    ISPCSI2_CTX_DAT_OFST(ctxnum));
		reg &= ~ISPCSI2_CTX_DAT_OFST_OFST_MASK;
		reg |= selected_ctx->data_offset <<
			ISPCSI2_CTX_DAT_OFST_OFST_SHIFT;
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_DAT_OFST(ctxnum));
		selected_ctx_u->data_offset = false;
	}

	if (selected_ctx_u->ping_addr) {
		reg = selected_ctx->ping_addr;
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_DAT_PING_ADDR(ctxnum));
		selected_ctx_u->ping_addr = false;
	}

	if (selected_ctx_u->pong_addr) {
		reg = selected_ctx->pong_addr;
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_DAT_PONG_ADDR(ctxnum));
		selected_ctx_u->pong_addr = false;
	}
	return 0;
}

/**
 * isp_csi2_ctx_get - Gets specific CSI2 Context configuration
 * @ctxnum: Context number, valid between 0 and 7 values.
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_get(u8 ctxnum)
{
	struct isp_csi2_ctx_cfg *selected_ctx;
	struct isp_csi2_ctx_cfg_update *selected_ctx_u;
	u32 reg;

	isp_csi2_ctx_validate(&ctxnum);

	selected_ctx = &current_csi2_cfg.contexts[ctxnum];
	selected_ctx_u = &current_csi2_cfg_update.contexts[ctxnum];

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTX_CTRL1(ctxnum));
	selected_ctx->frame_count = (reg & ISPCSI2_CTX_CTRL1_COUNT_MASK) >>
		ISPCSI2_CTX_CTRL1_COUNT_SHIFT;
	selected_ctx_u->frame_count = false;

	if ((reg & ISPCSI2_CTX_CTRL1_EOF_EN_MASK) ==
	    ISPCSI2_CTX_CTRL1_EOF_EN_ENABLE)
		selected_ctx->eof_enabled = true;
	else
		selected_ctx->eof_enabled = false;
	selected_ctx_u->eof_enabled = false;

	if ((reg & ISPCSI2_CTX_CTRL1_EOL_EN_MASK) ==
	    ISPCSI2_CTX_CTRL1_EOL_EN_ENABLE)
		selected_ctx->eol_enabled = true;
	else
		selected_ctx->eol_enabled = false;
	selected_ctx_u->eol_enabled = false;

	if ((reg & ISPCSI2_CTX_CTRL1_CS_EN_MASK) ==
	    ISPCSI2_CTX_CTRL1_CS_EN_ENABLE)
		selected_ctx->checksum_enabled = true;
	else
		selected_ctx->checksum_enabled = false;
	selected_ctx_u->checksum_enabled = false;

	if ((reg & ISPCSI2_CTX_CTRL1_CTX_EN_MASK) ==
	    ISPCSI2_CTX_CTRL1_CTX_EN_ENABLE)
		selected_ctx->enabled = true;
	else
		selected_ctx->enabled = false;
	selected_ctx_u->enabled = false;
	update_ctx_ctrl1[ctxnum] = false;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTX_CTRL2(ctxnum));

	selected_ctx->virtual_id = (reg & ISPCSI2_CTX_CTRL2_VIRTUAL_ID_MASK) >>
		ISPCSI2_CTX_CTRL2_VIRTUAL_ID_SHIFT;
	selected_ctx_u->virtual_id = false;

	switch ((reg & ISPCSI2_CTX_CTRL2_FORMAT_MASK) >>
		ISPCSI2_CTX_CTRL2_FORMAT_SHIFT) {
	case 0x22:
		selected_ctx->format.pixelformat = V4L2_PIX_FMT_RGB565;
		break;
	case 0x9E:
	case 0x1E:
		selected_ctx->format.pixelformat = V4L2_PIX_FMT_YUYV;
		break;
	case 0xA1:
		selected_ctx->format.pixelformat = V4L2_PIX_FMT_RGB555;
		break;
	case 0xAB:
	case 0x12F:
		selected_ctx->format.pixelformat = V4L2_PIX_FMT_SGRBG10;
		break;
	}
	selected_ctx_u->format = false;
	update_ctx_ctrl2[ctxnum] = false;

	selected_ctx->alpha = (isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
					     ISPCSI2_CTX_CTRL3(ctxnum)) &
			       ISPCSI2_CTX_CTRL3_ALPHA_MASK) >>
		ISPCSI2_CTX_CTRL3_ALPHA_SHIFT;
	selected_ctx_u->alpha = false;
	update_ctx_ctrl3[ctxnum] = false;

	selected_ctx->data_offset = (isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
						   ISPCSI2_CTX_DAT_OFST(ctxnum)) &
				     ISPCSI2_CTX_DAT_OFST_OFST_MASK) >>
		ISPCSI2_CTX_DAT_OFST_OFST_SHIFT;
	selected_ctx_u->data_offset = false;

	selected_ctx->ping_addr = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
						ISPCSI2_CTX_DAT_PING_ADDR(ctxnum));
	selected_ctx_u->ping_addr = false;

	selected_ctx->pong_addr = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
						ISPCSI2_CTX_DAT_PONG_ADDR(ctxnum));
	selected_ctx_u->pong_addr = false;
	return 0;
}

/**
 * isp_csi2_ctx_update_all - Applies all CSI2 context configuration.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_ctx_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_ctx_config_*() functions, unless the force_update flag is
 * set to true.
 * Always returns 0.
 **/
int isp_csi2_ctx_update_all(bool force_update)
{
	u8 ctxnum;

	for (ctxnum = 0; ctxnum < 8; ctxnum++)
		isp_csi2_ctx_update(ctxnum, force_update);

	return 0;
}

/**
 * isp_csi2_ctx_get_all - Gets all CSI2 Context configurations
 *
 * Always returns 0.
 **/
int isp_csi2_ctx_get_all(void)
{
	u8 ctxnum;

	for (ctxnum = 0; ctxnum < 8; ctxnum++)
		isp_csi2_ctx_get(ctxnum);

	return 0;
}

int isp_csi2_phy_config(struct isp_csi2_phy_cfg *desiredphyconfig)
{
	struct isp_csi2_phy_cfg *currphy = &current_csi2_cfg.phy;
	struct isp_csi2_phy_cfg_update *currphy_u =
						&current_csi2_cfg_update.phy;

	if ((desiredphyconfig->tclk_term > 0x7f) ||
				(desiredphyconfig->tclk_miss > 0x3)) {
		printk(KERN_ERR "Invalid PHY configuration sent by the"
								" driver\n");
		return -EINVAL;
	}

	if (currphy->ths_term != desiredphyconfig->ths_term) {
		currphy->ths_term = desiredphyconfig->ths_term;
		currphy_u->ths_term = true;
		update_phy_cfg0 = true;
	}
	if (currphy->ths_settle != desiredphyconfig->ths_settle) {
		currphy->ths_settle = desiredphyconfig->ths_settle;
		currphy_u->ths_settle = true;
		update_phy_cfg0 = true;
	}
	if (currphy->tclk_term != desiredphyconfig->tclk_term) {
		currphy->tclk_term = desiredphyconfig->tclk_term;
		currphy_u->tclk_term = true;
		update_phy_cfg1 = true;
	}
	if (currphy->tclk_miss != desiredphyconfig->tclk_miss) {
		currphy->tclk_miss = desiredphyconfig->tclk_miss;
		currphy_u->tclk_miss = true;
		update_phy_cfg1 = true;
	}
	if (currphy->tclk_settle != desiredphyconfig->tclk_settle) {
		currphy->tclk_settle = desiredphyconfig->tclk_settle;
		currphy_u->tclk_settle = true;
		update_phy_cfg1 = true;
	}
	return 0;
}

/**
 * isp_csi2_calc_phy_cfg0 - Calculates D-PHY config based on the MIPIClk speed.
 * @mipiclk: MIPI clock frequency being used with CSI2 sensor.
 * @lbound_hs_settle: Lower bound for CSI2 High Speed Settle transition.
 * @ubound_hs_settle: Upper bound for CSI2 High Speed Settle transition.
 *
 * From TRM, we have the same calculation for HS Termination signal.
 *  THS_TERM  = ceil( 12.5ns / DDRCLK period ) - 1
 * But for Settle, we use the mid value between the two passed boundaries from
 * sensor:
 *  THS_SETTLE = (Upper bound + Lower bound) / 2
 *
 * Always returns 0.
 */
int isp_csi2_calc_phy_cfg0(u32 mipiclk, u32 lbound_hs_settle,
							u32 ubound_hs_settle)
{
	struct isp_csi2_phy_cfg *currphy = &current_csi2_cfg.phy;
	struct isp_csi2_phy_cfg_update *currphy_u =
						&current_csi2_cfg_update.phy;
	u32 tmp, ddrclk = mipiclk >> 1;

	/* Calculate THS_TERM */
	tmp = ddrclk / 80000000;
	if ((ddrclk % 80000000) > 0)
		tmp++;
	currphy->ths_term = tmp - 1;
	currphy_u->ths_term = true;

	/* Calculate THS_SETTLE */
	currphy->ths_settle = (ubound_hs_settle + lbound_hs_settle) / 2;

	currphy_u->ths_settle = true;
	isp_csi2_phy_update(true);
	return 0;
}
EXPORT_SYMBOL(isp_csi2_calc_phy_cfg0);

/**
 * isp_csi2_phy_update - Applies CSI2 D-PHY configuration.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_phy_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_phy_config_*() functions, unless the force_update flag is
	* set to true.
	* Always returns 0.
	**/
int isp_csi2_phy_update(bool force_update)
{
	struct isp_csi2_phy_cfg *currphy = &current_csi2_cfg.phy;
	struct isp_csi2_phy_cfg_update *currphy_u =
		&current_csi2_cfg_update.phy;
	u32 reg;

	if (update_phy_cfg0 || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG0);
		if (currphy_u->ths_term || force_update) {
			reg &= ~ISPCSI2PHY_CFG0_THS_TERM_MASK;
			reg |= (currphy->ths_term <<
				ISPCSI2PHY_CFG0_THS_TERM_SHIFT);
			currphy_u->ths_term = false;
		}
		if (currphy_u->ths_settle || force_update) {
			reg &= ~ISPCSI2PHY_CFG0_THS_SETTLE_MASK;
			reg |= (currphy->ths_settle <<
				ISPCSI2PHY_CFG0_THS_SETTLE_SHIFT);
			currphy_u->ths_settle = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG0);
		update_phy_cfg0 = false;
	}

	if (update_phy_cfg1 || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG1);
		if (currphy_u->tclk_term || force_update) {
			reg &= ~ISPCSI2PHY_CFG1_TCLK_TERM_MASK;
			reg |= (currphy->tclk_term <<
				ISPCSI2PHY_CFG1_TCLK_TERM_SHIFT);
			currphy_u->tclk_term = false;
		}
		if (currphy_u->tclk_miss || force_update) {
			reg &= ~ISPCSI2PHY_CFG1_TCLK_MISS_MASK;
			reg |= (currphy->tclk_miss <<
				ISPCSI2PHY_CFG1_TCLK_MISS_SHIFT);
			currphy_u->tclk_miss = false;
		}
		if (currphy_u->tclk_settle || force_update) {
			reg &= ~ISPCSI2PHY_CFG1_TCLK_SETTLE_MASK;
			reg |= (currphy->tclk_settle <<
				ISPCSI2PHY_CFG1_TCLK_SETTLE_SHIFT);
			currphy_u->tclk_settle = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG1);
		update_phy_cfg1 = false;
	}
	return 0;
}

/**
 * isp_csi2_phy_get - Gets CSI2 D-PHY configuration
 *
 * Gets settings from HW registers and fills in the internal driver memory
 * Always returns 0.
 **/
int isp_csi2_phy_get(void)
{
	struct isp_csi2_phy_cfg *currphy = &current_csi2_cfg.phy;
	struct isp_csi2_phy_cfg_update *currphy_u =
		&current_csi2_cfg_update.phy;
	u32 reg;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG0);
	currphy->ths_term = (reg & ISPCSI2PHY_CFG0_THS_TERM_MASK) >>
		ISPCSI2PHY_CFG0_THS_TERM_SHIFT;
	currphy_u->ths_term = false;

	currphy->ths_settle = (reg & ISPCSI2PHY_CFG0_THS_SETTLE_MASK) >>
		ISPCSI2PHY_CFG0_THS_SETTLE_SHIFT;
	currphy_u->ths_settle = false;
	update_phy_cfg0 = false;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY, ISPCSI2PHY_CFG1);

	currphy->tclk_term = (reg & ISPCSI2PHY_CFG1_TCLK_TERM_MASK) >>
		ISPCSI2PHY_CFG1_TCLK_TERM_SHIFT;
	currphy_u->tclk_term = false;

	currphy->tclk_miss = (reg & ISPCSI2PHY_CFG1_TCLK_MISS_MASK) >>
		ISPCSI2PHY_CFG1_TCLK_MISS_SHIFT;
	currphy_u->tclk_miss = false;

	currphy->tclk_settle = (reg & ISPCSI2PHY_CFG1_TCLK_SETTLE_MASK) >>
		ISPCSI2PHY_CFG1_TCLK_SETTLE_SHIFT;
	currphy_u->tclk_settle = false;

	update_phy_cfg1 = false;
	return 0;
}

/**
 * isp_csi2_timings_config_forcerxmode - Sets Force Rx mode on stop state count
 * @force_rx_mode: Boolean to enable/disable forcing Rx mode in CSI2 receiver
 *
 * Returns 0 if successful, or -EINVAL if wrong ComplexIO number is selected.
 **/
int isp_csi2_timings_config_forcerxmode(u8 io, bool force_rx_mode)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];
	if (currtimings->force_rx_mode != force_rx_mode) {
		currtimings->force_rx_mode = force_rx_mode;
		currtimings_u->force_rx_mode = true;
		update_timing = true;
	}
	return 0;
}

/**
 * isp_csi2_timings_config_stopstate_16x - Sets 16x factor for L3 cycles
 * @stop_state_16x: Boolean to use or not use the 16x multiplier for stop count
 *
 * Returns 0 if successful, or -EINVAL if wrong ComplexIO number is selected.
 **/
int isp_csi2_timings_config_stopstate_16x(u8 io, bool stop_state_16x)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];
	if (currtimings->stop_state_16x != stop_state_16x) {
		currtimings->stop_state_16x = stop_state_16x;
		currtimings_u->stop_state_16x = true;
		update_timing = true;
	}
	return 0;
}

/**
 * isp_csi2_timings_config_stopstate_4x - Sets 4x factor for L3 cycles
 * @stop_state_4x: Boolean to use or not use the 4x multiplier for stop count
 *
 * Returns 0 if successful, or -EINVAL if wrong ComplexIO number is selected.
 **/
int isp_csi2_timings_config_stopstate_4x(u8 io, bool stop_state_4x)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];
	if (currtimings->stop_state_4x != stop_state_4x) {
		currtimings->stop_state_4x = stop_state_4x;
		currtimings_u->stop_state_4x = true;
		update_timing = true;
	}
	return 0;
}

/**
 * isp_csi2_timings_config_stopstate_cnt - Sets L3 cycles
 * @stop_state_counter: Stop state counter value for L3 cycles
 *
 * Returns 0 if successful, or -EINVAL if wrong ComplexIO number is selected.
 **/
int isp_csi2_timings_config_stopstate_cnt(u8 io, u16 stop_state_counter)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];
	if (currtimings->stop_state_counter != stop_state_counter) {
		currtimings->stop_state_counter = (stop_state_counter & 0x1FFF);
		currtimings_u->stop_state_counter = true;
		update_timing = true;
	}
	return 0;
}

/**
 * isp_csi2_timings_update - Applies specified CSI2 timing configuration.
 * @io: IO number (1 or 2) which specifies which ComplexIO are we updating
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_timings_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_timings_config_*() functions, unless the force_update flag is
 * set to true.
 * Returns 0 if successful, or -EINVAL if invalid IO number is passed.
 **/
int isp_csi2_timings_update(u8 io, bool force_update)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;
	u32 reg;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];

	if (update_timing || force_update) {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_TIMING);
		if (currtimings_u->force_rx_mode || force_update) {
			reg &= ~ISPCSI2_TIMING_FORCE_RX_MODE_IO_MASK(io);
			if (currtimings->force_rx_mode)
				reg |= ISPCSI2_TIMING_FORCE_RX_MODE_IO_ENABLE
					(io);
			else
				reg |= ISPCSI2_TIMING_FORCE_RX_MODE_IO_DISABLE
					(io);
			currtimings_u->force_rx_mode = false;
		}
		if (currtimings_u->stop_state_16x || force_update) {
			reg &= ~ISPCSI2_TIMING_STOP_STATE_X16_IO_MASK(io);
			if (currtimings->stop_state_16x)
				reg |= ISPCSI2_TIMING_STOP_STATE_X16_IO_ENABLE
					(io);
			else
				reg |= ISPCSI2_TIMING_STOP_STATE_X16_IO_DISABLE
					(io);
			currtimings_u->stop_state_16x = false;
		}
		if (currtimings_u->stop_state_4x || force_update) {
			reg &= ~ISPCSI2_TIMING_STOP_STATE_X4_IO_MASK(io);
			if (currtimings->stop_state_4x) {
				reg |= ISPCSI2_TIMING_STOP_STATE_X4_IO_ENABLE
					(io);
			} else {
				reg |= ISPCSI2_TIMING_STOP_STATE_X4_IO_DISABLE
					(io);
			}
			currtimings_u->stop_state_4x = false;
		}
		if (currtimings_u->stop_state_counter || force_update) {
			reg &= ~ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_MASK(io);
			reg |= currtimings->stop_state_counter <<
				ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_SHIFT(io);
			currtimings_u->stop_state_counter = false;
		}
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_TIMING);
		update_timing = false;
	}
	return 0;
}

/**
 * isp_csi2_timings_get - Gets specific CSI2 ComplexIO timing configuration
 * @io: IO number (1 or 2) which specifies which ComplexIO are we getting
 *
 * Gets settings from HW registers and fills in the internal driver memory
 * Returns 0 if successful, or -EINVAL if invalid IO number is passed.
 **/
int isp_csi2_timings_get(u8 io)
{
	struct isp_csi2_timings_cfg *currtimings;
	struct isp_csi2_timings_cfg_update *currtimings_u;
	u32 reg;

	if (io < 1 || io > 2) {
		printk(KERN_ERR "CSI2 - Timings config: Invalid IO number\n");
		return -EINVAL;
	}

	currtimings = &current_csi2_cfg.timings[io - 1];
	currtimings_u = &current_csi2_cfg_update.timings[io - 1];

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_TIMING);
	if ((reg & ISPCSI2_TIMING_FORCE_RX_MODE_IO_MASK(io)) ==
	    ISPCSI2_TIMING_FORCE_RX_MODE_IO_ENABLE(io))
		currtimings->force_rx_mode = true;
	else
		currtimings->force_rx_mode = false;
	currtimings_u->force_rx_mode = false;

	if ((reg & ISPCSI2_TIMING_STOP_STATE_X16_IO_MASK(io)) ==
	    ISPCSI2_TIMING_STOP_STATE_X16_IO_ENABLE(io))
		currtimings->stop_state_16x = true;
	else
		currtimings->stop_state_16x = false;
	currtimings_u->stop_state_16x = false;

	if ((reg & ISPCSI2_TIMING_STOP_STATE_X4_IO_MASK(io)) ==
	    ISPCSI2_TIMING_STOP_STATE_X4_IO_ENABLE(io))
		currtimings->stop_state_4x = true;
	else
		currtimings->stop_state_4x = false;
	currtimings_u->stop_state_4x = false;

	currtimings->stop_state_counter = (reg &
					   ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_MASK(io)) >>
		ISPCSI2_TIMING_STOP_STATE_COUNTER_IO_SHIFT(io);
	currtimings_u->stop_state_counter = false;
	update_timing = false;
	return 0;
}

/**
 * isp_csi2_timings_update_all - Applies specified CSI2 timing configuration.
 * @force_update: Flag to force rewrite of registers, even if they haven't been
 *                updated with the isp_csi2_timings_config_*() functions.
 *
 * It only saves settings when they were previously updated using the
 * isp_csi2_timings_config_*() functions, unless the force_update flag is
 * set to true.
 * Always returns 0.
 **/
int isp_csi2_timings_update_all(bool force_update)
{
	int i;

	for (i = 1; i < 3; i++)
		isp_csi2_timings_update(i, force_update);
	return 0;
}

/**
 * isp_csi2_timings_get_all - Gets all CSI2 ComplexIO timing configurations
 *
 * Always returns 0.
 **/
int isp_csi2_timings_get_all(void)
{
	int i;

	for (i = 1; i < 3; i++)
		isp_csi2_timings_get(i);
	return 0;
}

/**
 * isp_csi2_isr - CSI2 interrupt handling.
 **/
void isp_csi2_isr(void)
{
	u32 csi2_irqstatus, cpxio1_irqstatus, ctxirqstatus;

	csi2_irqstatus = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				       ISPCSI2_IRQSTATUS);
	isp_reg_writel(csi2_irqstatus, OMAP3_ISP_IOMEM_CSI2A,
		       ISPCSI2_IRQSTATUS);

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_COMPLEXIO1_ERR_IRQ) {
		cpxio1_irqstatus = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
						 ISPCSI2_COMPLEXIO1_IRQSTATUS);
		isp_reg_writel(cpxio1_irqstatus, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_COMPLEXIO1_IRQSTATUS);
		printk(KERN_ERR "CSI2: ComplexIO Error IRQ %x\n",
		       cpxio1_irqstatus);
	}

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_CONTEXT(0)) {
		ctxirqstatus = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
					     ISPCSI2_CTX_IRQSTATUS(0));
		isp_reg_writel(ctxirqstatus, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_IRQSTATUS(0));
	}

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_OCP_ERR_IRQ)
		printk(KERN_ERR "CSI2: OCP Transmission Error\n");

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_SHORT_PACKET_IRQ)
		printk(KERN_ERR "CSI2: Short packet receive error\n");

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_ECC_CORRECTION_IRQ)
		printk(KERN_DEBUG "CSI2: ECC correction done\n");

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_ECC_NO_CORRECTION_IRQ)
		printk(KERN_ERR "CSI2: ECC correction failed\n");

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_COMPLEXIO2_ERR_IRQ)
		printk(KERN_ERR "CSI2: ComplexIO #2 failed\n");

	if (csi2_irqstatus & ISPCSI2_IRQSTATUS_FIFO_OVF_IRQ)
		printk(KERN_ERR "CSI2: FIFO overflow error\n");

	return;
}
EXPORT_SYMBOL(isp_csi2_isr);

/**
 * isp_csi2_irq_complexio1_set - Enables CSI2 ComplexIO IRQs.
 * @enable: Enable/disable CSI2 ComplexIO #1 interrupts
 **/
void isp_csi2_irq_complexio1_set(int enable)
{
	u32 reg;
	reg = ISPCSI2_COMPLEXIO1_IRQENABLE_STATEALLULPMEXIT |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEALLULPMENTER |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEULPM5 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRCONTROL5 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRESC5 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTSYNCHS5 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTHS5 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEULPM4 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRCONTROL4 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRESC4 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTSYNCHS4 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTHS4 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEULPM3 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRCONTROL3 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRESC3 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTSYNCHS3 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTHS3 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEULPM2 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRCONTROL2 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRESC2 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTSYNCHS2 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTHS2 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_STATEULPM1 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRCONTROL1 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRESC1 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTSYNCHS1 |
		ISPCSI2_COMPLEXIO1_IRQENABLE_ERRSOTHS1;
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
		       ISPCSI2_COMPLEXIO1_IRQSTATUS);
	if (enable) {
		reg |= isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
				     ISPCSI2_COMPLEXIO1_IRQENABLE);
	} else
		reg = 0;
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
		       ISPCSI2_COMPLEXIO1_IRQENABLE);
}
EXPORT_SYMBOL(isp_csi2_irq_complexio1_set);

/**
 * isp_csi2_irq_ctx_set - Enables CSI2 Context IRQs.
 * @enable: Enable/disable CSI2 Context interrupts
 **/
void isp_csi2_irq_ctx_set(int enable)
{
	u32 reg;
	int i;

	reg = ISPCSI2_CTX_IRQSTATUS_FS_IRQ | ISPCSI2_CTX_IRQSTATUS_FE_IRQ;
	for (i = 0; i < 8; i++) {
		isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A,
			       ISPCSI2_CTX_IRQSTATUS(i));
		if (enable) {
			isp_reg_or(OMAP3_ISP_IOMEM_CSI2A,
				   ISPCSI2_CTX_IRQENABLE(i), reg);
		} else {
			isp_reg_writel(0, OMAP3_ISP_IOMEM_CSI2A,
				       ISPCSI2_CTX_IRQENABLE(i));
		}
	}

}
EXPORT_SYMBOL(isp_csi2_irq_ctx_set);

/**
 * isp_csi2_irq_status_set - Enables CSI2 Status IRQs.
 * @enable: Enable/disable CSI2 Status interrupts
 **/
void isp_csi2_irq_status_set(int enable)
{
	u32 reg;
	reg = ISPCSI2_IRQSTATUS_OCP_ERR_IRQ |
		ISPCSI2_IRQSTATUS_SHORT_PACKET_IRQ |
		ISPCSI2_IRQSTATUS_ECC_CORRECTION_IRQ |
		ISPCSI2_IRQSTATUS_ECC_NO_CORRECTION_IRQ |
		ISPCSI2_IRQSTATUS_COMPLEXIO2_ERR_IRQ |
		ISPCSI2_IRQSTATUS_COMPLEXIO1_ERR_IRQ |
		ISPCSI2_IRQSTATUS_FIFO_OVF_IRQ |
		ISPCSI2_IRQSTATUS_CONTEXT(0);
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_IRQSTATUS);
	if (enable)
		reg |= isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_IRQENABLE);
	else
		reg = 0;

	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_IRQENABLE);
}
EXPORT_SYMBOL(isp_csi2_irq_status_set);

/**
 * isp_csi2_irq_status_set - Enables main CSI2 IRQ.
 * @enable: Enable/disable main CSI2 interrupt
 **/
void isp_csi2_irq_set(int enable)
{
	isp_reg_writel(IRQ0STATUS_CSIA_IRQ, OMAP3_ISP_IOMEM_MAIN,
		       ISP_IRQ0STATUS);
	isp_reg_and_or(OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE,
		       ~IRQ0ENABLE_CSIA_IRQ,
		       (enable ? IRQ0ENABLE_CSIA_IRQ : 0));
}
EXPORT_SYMBOL(isp_csi2_irq_set);

/**
 * isp_csi2_irq_all_set - Enable/disable CSI2 interrupts.
 * @enable: 0-Disable, 1-Enable.
 **/
void isp_csi2_irq_all_set(int enable)
{
	if (enable) {
		isp_csi2_irq_complexio1_set(enable);
		isp_csi2_irq_ctx_set(enable);
		isp_csi2_irq_status_set(enable);
		isp_csi2_irq_set(enable);
	} else {
		isp_csi2_irq_set(enable);
		isp_csi2_irq_status_set(enable);
		isp_csi2_irq_ctx_set(enable);
		isp_csi2_irq_complexio1_set(enable);
	}
	return;
}
EXPORT_SYMBOL(isp_csi2_irq_all_set);

/**
 * isp_csi2_reset - Resets the CSI2 module.
 *
 * Returns 0 if successful, or -EBUSY if power command didn't respond.
 **/
int isp_csi2_reset(void)
{
	u32 reg;
	u8 soft_reset_retries = 0;
	int i;

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_SYSCONFIG);
	reg |= ISPCSI2_SYSCONFIG_SOFT_RESET_RESET;
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_SYSCONFIG);

	do {
		reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_SYSSTATUS) &
			ISPCSI2_SYSSTATUS_RESET_DONE_MASK;
		if (reg == ISPCSI2_SYSSTATUS_RESET_DONE_DONE)
			break;
		soft_reset_retries++;
		if (soft_reset_retries < 5)
			udelay(100);
	} while (soft_reset_retries < 5);

	if (soft_reset_retries == 5) {
		printk(KERN_ERR "CSI2: Soft reset try count exceeded!\n");
		return -EBUSY;
	}

	reg = isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_SYSCONFIG);
	reg &= ~ISPCSI2_SYSCONFIG_MSTANDBY_MODE_MASK;
	reg |= ISPCSI2_SYSCONFIG_MSTANDBY_MODE_NO;
	reg &= ~ISPCSI2_SYSCONFIG_AUTO_IDLE_MASK;
	isp_reg_writel(reg, OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_SYSCONFIG);

	uses_videoport = false;
	update_complexio_cfg1 = false;
	update_phy_cfg0 = false;
	update_phy_cfg1 = false;
	for (i = 0; i < 8; i++) {
		update_ctx_ctrl1[i] = false;
		update_ctx_ctrl2[i] = false;
		update_ctx_ctrl3[i] = false;
	}
	update_timing = false;
	update_ctrl = false;

	isp_csi2_complexio_lanes_get();
	isp_csi2_ctrl_get();
	isp_csi2_ctx_get_all();
	isp_csi2_phy_get();
	isp_csi2_timings_get_all();

	isp_csi2_complexio_power_autoswitch(true);
	isp_csi2_complexio_power(ISP_CSI2_POWER_ON);

	isp_csi2_timings_config_forcerxmode(1, true);
	isp_csi2_timings_config_stopstate_cnt(1, 0x1FF);
	isp_csi2_timings_update_all(true);

	return 0;
}

/**
 * isp_csi2_enable - Enables the CSI2 module.
 * @enable: Enables/disables the CSI2 module.
 **/
void isp_csi2_enable(int enable)
{
	if (enable) {
		isp_csi2_ctx_config_enabled(0, true);
		isp_csi2_ctx_config_eof_enabled(0, true);
		isp_csi2_ctx_config_checksum_enabled(0, true);
		isp_csi2_ctx_update(0, false);

		isp_csi2_ctrl_config_ecc_enable(true);
		isp_csi2_ctrl_config_if_enable(true);
		isp_csi2_ctrl_update(false);
	} else {
		isp_csi2_ctx_config_enabled(0, false);
		isp_csi2_ctx_config_eof_enabled(0, false);
		isp_csi2_ctx_config_checksum_enabled(0, false);
		isp_csi2_ctx_update(0, false);

		isp_csi2_ctrl_config_ecc_enable(false);
		isp_csi2_ctrl_config_if_enable(false);
		isp_csi2_ctrl_update(false);
	}
}
EXPORT_SYMBOL(isp_csi2_enable);

/**
 * isp_csi2_regdump - Prints CSI2 debug information.
 **/
void isp_csi2_regdump(void)
{
	printk(KERN_DEBUG "-------------Register dump-------------\n");

	printk(KERN_DEBUG "ISP_CTRL: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_MAIN, ISP_CTRL));
	printk(KERN_DEBUG "ISP_TCTRL_CTRL: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_MAIN, ISP_TCTRL_CTRL));

	printk(KERN_DEBUG "ISPCCDC_SDR_ADDR: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_SDR_ADDR));
	printk(KERN_DEBUG "ISPCCDC_SYN_MODE: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_SYN_MODE));
	printk(KERN_DEBUG "ISPCCDC_CFG: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_CFG));
	printk(KERN_DEBUG "ISPCCDC_FMTCFG: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_FMTCFG));
	printk(KERN_DEBUG "ISPCCDC_HSIZE_OFF: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_HSIZE_OFF));
	printk(KERN_DEBUG "ISPCCDC_HORZ_INFO: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC, ISPCCDC_HORZ_INFO));
	printk(KERN_DEBUG "ISPCCDC_VERT_START: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC,
			     ISPCCDC_VERT_START));
	printk(KERN_DEBUG "ISPCCDC_VERT_LINES: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CCDC,
			     ISPCCDC_VERT_LINES));

	printk(KERN_DEBUG "ISPCSI2_COMPLEXIO_CFG1: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_COMPLEXIO_CFG1));
	printk(KERN_DEBUG "ISPCSI2_SYSSTATUS: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_SYSSTATUS));
	printk(KERN_DEBUG "ISPCSI2_SYSCONFIG: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_SYSCONFIG));
	printk(KERN_DEBUG "ISPCSI2_IRQENABLE: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_IRQENABLE));
	printk(KERN_DEBUG "ISPCSI2_IRQSTATUS: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_IRQSTATUS));

	printk(KERN_DEBUG "ISPCSI2_CTX_IRQENABLE(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_IRQENABLE(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_IRQSTATUS(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_IRQSTATUS(0)));
	printk(KERN_DEBUG "ISPCSI2_TIMING: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_TIMING));
	printk(KERN_DEBUG "ISPCSI2PHY_CFG0: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY,
			     ISPCSI2PHY_CFG0));
	printk(KERN_DEBUG "ISPCSI2PHY_CFG1: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2PHY,
			     ISPCSI2PHY_CFG1));
	printk(KERN_DEBUG "ISPCSI2_CTX_CTRL1(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_CTRL1(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_CTRL2(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_CTRL2(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_CTRL3(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_CTRL3(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_DAT_OFST(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_DAT_OFST(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_DAT_PING_ADDR(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_DAT_PING_ADDR(0)));
	printk(KERN_DEBUG "ISPCSI2_CTX_DAT_PONG_ADDR(0): %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A,
			     ISPCSI2_CTX_DAT_PONG_ADDR(0)));
	printk(KERN_DEBUG "ISPCSI2_CTRL: %x\n",
	       isp_reg_readl(OMAP3_ISP_IOMEM_CSI2A, ISPCSI2_CTRL));
	printk(KERN_DEBUG "---------------------------------------\n");
}

/**
 * isp_csi2_cleanup - Routine for module driver cleanup
 **/
void isp_csi2_cleanup(void)
{
	return;
}

/**
 * isp_csi2_init - Routine for module driver init
 **/
int __init isp_csi2_init(void)
{
	int i;

	update_complexio_cfg1 = false;
	update_phy_cfg0 = false;
	update_phy_cfg1 = false;
	for (i = 0; i < 8; i++) {
		update_ctx_ctrl1[i] = false;
		update_ctx_ctrl2[i] = false;
		update_ctx_ctrl3[i] = false;
	}
	update_timing = false;
	update_ctrl = false;

	memset(&current_csi2_cfg, 0, sizeof(current_csi2_cfg));
	memset(&current_csi2_cfg_update, 0, sizeof(current_csi2_cfg_update));
	return 0;
}

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("ISP CSI2 Receiver Module");
MODULE_LICENSE("GPL");
