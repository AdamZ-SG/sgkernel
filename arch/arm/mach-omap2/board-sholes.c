/*
 * linux/arch/arm/mach-omap2/board-sholes.c
 *
 * Copyright (C) 2007-2009 Motorola, Inc.
 *
 * Modified from mach-omap3/board-3430sdp.c
 *
 * Copyright (C) 2007 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/mm.h>
#include <linux/qtouch_obp_ts.h>
#include <linux/led-lm3530.h>
#include <linux/usb/omap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <mach/board-sholes.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>
#include <mach/usb.h>
#include <asm/delay.h>
#include <mach/control.h>
#include <mach/hdq.h>

#include "pm.h"
#include "prm-regbits-34xx.h"

#define SHOLES_IPC_USB_SUSP_GPIO	142
#define SHOLES_AP_TO_BP_FLASH_EN_GPIO	157
#define SHOLES_TOUCH_RESET_N_GPIO	164
#define SHOLES_TOUCH_INT_GPIO		99
#define SHOLES_LM_3530_INT_GPIO		92
#define SHOLES_AKM8973_INT_GPIO		175

static void __init sholes_init_irq(void)
{
	omap2_init_common_hw(NULL, NULL, NULL, NULL);
	omap_init_irq();
#ifdef CONFIG_OMAP3_PM
	scm_clk_init();
#endif
	omap_gpio_init();
}

static struct omap_uart_config sholes_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel sholes_config[] __initdata = {
	{OMAP_TAG_UART,		&sholes_uart_config },
};

static int sholes_touch_reset(void)
{
	gpio_direction_output(SHOLES_TOUCH_RESET_N_GPIO, 1);
	msleep(1);
	gpio_set_value(SHOLES_TOUCH_RESET_N_GPIO, 0);
	msleep(20);
	gpio_set_value(SHOLES_TOUCH_RESET_N_GPIO, 1);
	msleep(20);

	return 0;
}

static void sholes_touch_init(void)
{
	gpio_request(SHOLES_TOUCH_RESET_N_GPIO, "sholes touch reset");
	gpio_direction_output(SHOLES_TOUCH_RESET_N_GPIO, 1);
	omap_cfg_reg(H19_34XX_GPIO164_OUT);

	gpio_request(SHOLES_TOUCH_INT_GPIO, "sholes touch irq");
	gpio_direction_input(SHOLES_TOUCH_INT_GPIO);
	omap_cfg_reg(AG17_34XX_GPIO99);
}

static void sholes_als_init(void)
{
	printk(KERN_INFO "%s:Initializing\n", __func__);
	gpio_request(SHOLES_LM_3530_INT_GPIO, "sholes als int");
	gpio_direction_input(SHOLES_LM_3530_INT_GPIO);
	omap_cfg_reg(AC27_34XX_GPIO92);
}

static struct vkey sholes_touch_vkeys[] = {
	{
		.min		= 44,
		.max		= 152,
		.code		= KEY_BACK,
	},
	{
		.min		= 321,
		.max		= 429,
		.code		= KEY_MENU,
	},
	{
		.min		= 606,
		.max		= 714,
		.code		= KEY_HOME,
	},
	{
		.min		= 886,
		.max		= 994,
		.code		= KEY_SEARCH,
	},
};

static struct qtouch_ts_platform_data sholes_ts_platform_data = {
	.irqflags	= IRQF_TRIGGER_LOW,
	.flags		= (QTOUCH_SWAP_XY |
			   QTOUCH_USE_MULTITOUCH |
			   QTOUCH_CFG_BACKUPNV),
	.abs_min_x	= 0,
	.abs_max_x	= 1024,
	.abs_min_y	= 0,
	.abs_max_y	= 960,
	.abs_min_p	= 0,
	.abs_max_p	= 255,
	.abs_min_w	= 0,
	.abs_max_w	= 15,
	.nv_checksum	= 0xbbaf,
	.fuzz_x		= 0,
	.fuzz_y		= 0,
	.fuzz_p		= 2,
	.fuzz_w		= 2,
	.hw_reset	= sholes_touch_reset,
	.power_cfg	= {
		.idle_acq_int	= 1,
		.active_acq_int	= 16,
		.active_idle_to	= 25,
	},
	.acquire_cfg	= {
		.charge_time	= 10,
		.atouch_drift	= 5,
		.touch_drift	= 20,
		.drift_susp	= 20,
		.touch_autocal	= 0,
		.sync		= 0,
	},
	.multi_touch_cfg	= {
		.x_origin	= 0,
		.y_origin	= 0,
		.x_size		= 12,
		.y_size		= 7,
		.aks_cfg	= 1,
		.burst_len	= 0x40,
		.tch_det_thr	= 0xf,
		.tch_det_int	= 0x2,
		.mov_hyst_init	= 5,
		.mov_hyst_next	= 5,
		.mov_filter	= 0,
		.num_touch	= 4,
		.merge_hyst	= 0,
		.merge_thresh	= 3,
	},
	.vkeys			= {
		.keys		= sholes_touch_vkeys,
		.count		= ARRAY_SIZE(sholes_touch_vkeys),
		.start		= 961,
	},
};

static struct lm3530_platform_data omap3430_als_light_data = {
	.gen_config = 0x33,
	.als_config = 0x7D,
	.brightness_ramp = 0x36,
	.als_zone_info = 0x00,
	.als_resistor_sel = 0x66,
	.brightness_control = 0x00,
	.zone_boundary_0 = 0x33,
	.zone_boundary_1 = 0x66,
	.zone_boundary_2 = 0x99,
	.zone_boundary_3 = 0xCC,
	.zone_target_0 = 0x19,
	.zone_target_1 = 0x33,
	.zone_target_2 = 0x4c,
	.zone_target_3 = 0x66,
	.zone_target_4 = 0x7f,
	.manual_current = 0x33,
	.upper_curr_sel = 5,
	.lower_curr_sel = 2,
};

static struct i2c_board_info __initdata sholes_i2c_bus1_board_info[] = {
	{
		I2C_BOARD_INFO(QTOUCH_TS_NAME, 0x11),
		.platform_data = &sholes_ts_platform_data,
		.irq = OMAP_GPIO_IRQ(SHOLES_TOUCH_INT_GPIO),
	},
	{
		I2C_BOARD_INFO(LD_LM3530_NAME, 0x38),
		.platform_data = &omap3430_als_light_data,
		.irq = OMAP_GPIO_IRQ(SHOLES_LM_3530_INT_GPIO),
	},
};

extern struct akm8973_platform_data sholes_akm8973_data;
extern struct lis331dlh_platform_data sholes_lis331dlh_data;
static struct i2c_board_info __initdata sholes_i2c_bus2_board_info[] = {
	{
		I2C_BOARD_INFO("akm8973", 0x1C),
		.platform_data = &sholes_akm8973_data,
		.irq = OMAP_GPIO_IRQ(SHOLES_AKM8973_INT_GPIO),
	},
	{
		I2C_BOARD_INFO("lis331dlh", 0x19),
		.platform_data = &sholes_lis331dlh_data,
	},	
};

static int __init sholes_i2c_init(void)
{
	omap_register_i2c_bus(1, 400, sholes_i2c_bus1_board_info,
			      ARRAY_SIZE(sholes_i2c_bus1_board_info));
	omap_register_i2c_bus(2, 400, sholes_i2c_bus2_board_info,
			      ARRAY_SIZE(sholes_i2c_bus2_board_info));
	return 0;
}

arch_initcall(sholes_i2c_init);

extern void __init sholes_spi_init(void);
extern void __init sholes_flash_init(void);
extern void __init sholes_gpio_iomux_init(void);



#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_EHCI_HCD_MODULE)

static int sholes_usb_port_startup(struct platform_device *dev, int port)
{
	int r;

	if (port == 2) {
		r = gpio_request(SHOLES_IPC_USB_SUSP_GPIO, "ipc_usb_susp");
		if (r < 0) {
			printk(KERN_WARNING "Could not request GPIO %d"
			       " for IPC_USB_SUSP\n",
			       SHOLES_IPC_USB_SUSP_GPIO);
			return r;
		}
		gpio_direction_output(SHOLES_IPC_USB_SUSP_GPIO, 0);
	} else {
		return -EINVAL;
	}
	return 0;
}

static void sholes_usb_port_shutdown(struct platform_device *dev, int port)
{
	if (port == 2)
		gpio_free(SHOLES_IPC_USB_SUSP_GPIO);
}


static void sholes_usb_port_suspend(struct platform_device *dev,
				    int port, int suspend)
{
	if (port == 2)
		gpio_set_value(SHOLES_IPC_USB_SUSP_GPIO, suspend);
}


static struct omap_usb_port_data usb_port_data[] = {
	[0] = { .flags = 0x0, }, /* disabled */
	[1] = { .flags = 0x0, }, /* disabled */
	[2] = {
		.flags = OMAP_USB_PORT_FLAG_ENABLED |
			OMAP_USB_PORT_FLAG_NOBITSTUFF,
		.mode = OMAP_USB_PORT_MODE_UTMI_PHY_4PIN,
		.startup = sholes_usb_port_startup,
		.shutdown = sholes_usb_port_shutdown,
		.suspend = sholes_usb_port_suspend,
	},
};

static struct omap_usb_platform_data usb_platform_data = {
	.port_data = usb_port_data,
	.num_ports = ARRAY_SIZE(usb_port_data),
};

static struct resource ehci_resources[] = {
	[0] = {
		.start	= OMAP34XX_HSUSB_HOST_BASE + 0x800,
		.end	= OMAP34XX_HSUSB_HOST_BASE + 0x800 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {         /* general IRQ */
		.start	= INT_34XX_EHCI_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 ehci_dmamask = ~(u32)0;
static struct platform_device ehci_device = {
	.name		= "ehci-omap",
	.id		= 0,
	.dev = {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb_platform_data,
	},
	.num_resources	= ARRAY_SIZE(ehci_resources),
	.resource	= ehci_resources,
};
#endif

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct resource ohci_resources[] = {
	[0] = {
		.start	= OMAP34XX_HSUSB_HOST_BASE + 0x400,
		.end	= OMAP34XX_HSUSB_HOST_BASE + 0x400 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {         /* general IRQ */
		.start	= INT_34XX_OHCI_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 ohci_dmamask = ~(u32)0;

static struct omap_usb_config dummy_usb_config = {
};

static struct platform_device ohci_device = {
	.name		= "ohci",
	.id		= 0,
	.dev = {
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data	= &dummy_usb_config,
	},
	.num_resources	= ARRAY_SIZE(ohci_resources),
	.resource	= ohci_resources,
};
#endif /* OHCI specific data */


static void __init sholes_ehci_init(void)
{

	omap_cfg_reg(AF5_34XX_GPIO142);		/*  IPC_USB_SUSP      */
	omap_cfg_reg(AA21_34XX_GPIO157);	/*  AP_TO_BP_FLASH_EN */
	omap_cfg_reg(AD1_3430_USB3FS_PHY_MM3_RXRCV);
	omap_cfg_reg(AD2_3430_USB3FS_PHY_MM3_TXDAT);
	omap_cfg_reg(AC1_3430_USB3FS_PHY_MM3_TXEN_N);
	omap_cfg_reg(AE1_3430_USB3FS_PHY_MM3_TXSE0);

	if (gpio_request(SHOLES_AP_TO_BP_FLASH_EN_GPIO,
			 "ap_to_bp_flash_en") != 0) {
		printk(KERN_WARNING "Could not request GPIO %d"
		       " for IPC_USB_SUSP\n",
		       SHOLES_IPC_USB_SUSP_GPIO);
		return;
	}
	gpio_direction_output(SHOLES_AP_TO_BP_FLASH_EN_GPIO, 0);

#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_EHCI_HCD_MODULE)
	platform_device_register(&ehci_device);
#endif
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	platform_device_register(&ohci_device);
#endif
}

static void __init sholes_sdrc_init(void)
{
	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_cfg_reg(H16_34XX_SDRC_CKE0);
	omap_cfg_reg(H17_34XX_SDRC_CKE1);
}

static void __init sholes_serial_init(void)
{
	omap_cfg_reg(AA8_3430_UART1_TX);
	omap_cfg_reg(Y8_3430_UART1_RX);
	omap_cfg_reg(AA9_3430_UART1_RTS);
	omap_cfg_reg(W8_3430_UART1_CTS);
	omap_serial_init();
}

/* SMPS I2C voltage control register Address for VDD1 */
#define SHOLES_R_VDD1_SR_CONTROL		0x00
/* SMPS I2C voltage control register Address for VDD2 */
#define SHOLES_R_VDD2_SR_CONTROL		0x00
/* SMPS I2C Address for VDD1 */
#define SHOLES_R_SRI2C_SLAVE_ADDR_SA0		0x1
/* SMPS I2C Address for VDD2 */
#define SHOLES_R_SRI2C_SLAVE_ADDR_SA1		0x2
/* SMPS I2C voltage control register Address for VDD1, used for SR command */
#define SHOLES_R_SMPS_VOL_CNTL_CMDRA0		0x01
/* SMPS I2C voltage control register Address for VDD2, used for SR command */
#define SHOLES_R_SMPS_VOL_CNTL_CMDRA1		0x01

static struct prm_setup_vc sholes_prm_setup = {
	.clksetup = 0x52,
	.voltsetup_time1 = 0x229,
	.voltsetup_time2 = 0x229,
	.voltoffset = 0x0,
	.voltsetup2 = 0x0,
	.vdd0_on = 0x65,
	.vdd0_onlp = 0x45,
	.vdd0_ret = 0x17,
	.vdd0_off = 0x00,
	.vdd1_on = 0x65,
	.vdd1_onlp = 0x45,
	.vdd1_ret = 0x17,
	.vdd1_off = 0x00,
	.i2c_slave_ra = (SHOLES_R_SRI2C_SLAVE_ADDR_SA1 <<
			OMAP3430_SMPS_SA1_SHIFT) |
			(SHOLES_R_SRI2C_SLAVE_ADDR_SA0 <<
			 OMAP3430_SMPS_SA0_SHIFT),
	.vdd_vol_ra = (SHOLES_R_VDD2_SR_CONTROL << OMAP3430_VOLRA1_SHIFT) |
			(SHOLES_R_VDD1_SR_CONTROL << OMAP3430_VOLRA0_SHIFT),
	/* vdd_vol_ra controls both cmd and vol, set the address equal */
	.vdd_cmd_ra = (SHOLES_R_SMPS_VOL_CNTL_CMDRA1 << OMAP3430_CMDRA1_SHIFT) |
		(SHOLES_R_SMPS_VOL_CNTL_CMDRA0 << OMAP3430_CMDRA0_SHIFT),
	.vdd_ch_conf = OMAP3430_CMD1 | OMAP3430_RACEN0 |
			OMAP3430_PRM_VC_CH_CONF_SA1 | OMAP3430_RACEN1 |
			OMAP3430_RAV1 | OMAP3430_RAC1, OMAP3430_GR_MOD,
	.vdd_i2c_cfg = OMAP3430_HSEN,
};

#define R_SMPS_VOL_OPP1_RA0		0x02
#define R_SMPS_VOL_OPP1_RA1		0x02
#define R_SMPS_VOL_OPP2_RA0		0x03
#define R_SMPS_VOL_OPP2_RA1		0x03

/* Sholes specific PM */
static void sholes_pm_init(void)
{
	omap3_set_prm_setup_vc(&sholes_prm_setup);

	/* Initialize CPCAP SW1&SW2 OPP1&OPP2 registers */
	/* SW1, OPP1 for RET Voltage --- 1.0V,
	 * OPP2 for ON Voltge --- 1.225V(OPP3)
	 */
	omap3_bypass_cmd(SHOLES_R_SRI2C_SLAVE_ADDR_SA0,
				R_SMPS_VOL_OPP1_RA0, 0x20);
	omap3_bypass_cmd(SHOLES_R_SRI2C_SLAVE_ADDR_SA0,
				R_SMPS_VOL_OPP2_RA0, 0x32);

	/* SW2, OPP1 for RET Voltage --- 1.0V,
	 * OPP2 for ON Voltge --- 1.175V(OPP3)
	 */
	omap3_bypass_cmd(SHOLES_R_SRI2C_SLAVE_ADDR_SA1,
				R_SMPS_VOL_OPP1_RA1, 0x20);
	omap3_bypass_cmd(SHOLES_R_SRI2C_SLAVE_ADDR_SA1,
				R_SMPS_VOL_OPP2_RA1, 0x2E);
}

static void __init config_wlan_gpio(void)
{
	/* WLAN PE and IRQ */
	omap_cfg_reg(AE22_3430_GPIO186_OUT);
	omap_cfg_reg(J8_3430_GPIO65);
}

static void __init config_mmc2_init(void)
{
	u32 val;

	/* MMC2 */
	omap_cfg_reg(AE2_3430_MMC2_CLK);
	omap_cfg_reg(AG5_3430_MMC2_CMD);
	omap_cfg_reg(AH5_3430_MMC2_DAT0);
	omap_cfg_reg(AH4_3430_MMC2_DAT1);
	omap_cfg_reg(AG4_3430_MMC2_DAT2);
	omap_cfg_reg(AF4_3430_MMC2_DAT3);

	/* Set internal loopback clock */
	val = omap_ctrl_readl(OMAP343X_CONTROL_DEVCONF1);
	omap_ctrl_writel((val | OMAP2_MMCSDIO2ADPCLKISEL),
				OMAP343X_CONTROL_DEVCONF1);
}

/* must match value in drivers/w1/w1_family.h */
#define W1_EEPROM_DS2502        0x89
static struct omap2_hdq_platform_config sholes_hdq_data = {
	.mode = OMAP_SDQ_MODE,
	.id = W1_EEPROM_DS2502,
};

static int __init omap_hdq_init(void)
{
	omap_hdq_device.dev.platform_data = &sholes_hdq_data;
	return platform_device_register(&omap_hdq_device);
}

static void __init sholes_init(void)
{
	omap_board_config = sholes_config;
	omap_board_config_size = ARRAY_SIZE(sholes_config);
	sholes_spi_init();
	sholes_flash_init();
	sholes_serial_init();
	sholes_als_init();
	sholes_panel_init();
	sholes_sensors_init();
	sholes_touch_init();
	usb_musb_init();
	sholes_ehci_init();
	sholes_sdrc_init();
	sholes_pm_init();
	config_mmc2_init();
	config_wlan_gpio();
	omap_hdq_init();
}

static void __init sholes_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(SHOLES, "sholes")
	/* Maintainer: Motorola, Inc. */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
#ifdef CONFIG_MACH_SHOLES_UMTS
	.boot_params	= 0x80C00100,
#else
	.boot_params	= 0x80000100,
#endif
	.map_io		= sholes_map_io,
	.init_irq	= sholes_init_irq,
	.init_machine	= sholes_init,
	.timer		= &omap_timer,
MACHINE_END
