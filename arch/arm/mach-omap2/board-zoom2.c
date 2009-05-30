/*
 * linux/arch/arm/mach-omap2/board-zoom2.c
 *
 * Copyright (C) 2008 Texas Instruments Inc.
 * Vikram Pandita <vikram.pandita@ti.com>
 *
 * Modified from mach-omap2/board-ldp.c
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
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/synaptics_i2c_rmi.h>
#include <linux/spi/spi.h>
#include <linux/i2c/twl4030.h>
#include <linux/interrupt.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board-zoom2.h>
#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>
#if 0
#include <mach/hsmmc.h>
#endif
#include <mach/usb.h>
#include <mach/mux.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <mach/control.h>

#include <mach/display.h>

#include "mmc-twl4030.h"

#ifndef CONFIG_TWL4030_CORE
#error "no power companion board defined!"
#endif


#define ZOOM2_QUART_PHYS        0x10000000
#define ZOOM2_QUART_VIRT        0xFB000000
#define ZOOM2_QUART_SIZE        SZ_1M

#define OMAP_SYNAPTICS_GPIO		163

#define SDP3430_SMC91X_CS	3
#define CONFIG_DISABLE_HFCLK 1
#define ENABLE_VAUX1_DEDICATED	0x03
#define ENABLE_VAUX1_DEV_GRP	0x20

#define ENABLE_VAUX3_DEDICATED  0x03
#define ENABLE_VAUX3_DEV_GRP  	0x20
#define TWL4030_MSECURE_GPIO	22

#define TWL4030_VAUX4_DEV_GRP	0x23
#define TWL4030_VAUX4_DEDICATED	0x26

static struct resource zoom2_smc911x_resources[] = {
	[0] = {
		.start	= OMAP34XX_ETHR_START,
		.end	= OMAP34XX_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device zoom2_smc911x_device = {
	.name		= "smc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(zoom2_smc911x_resources),
	.resource	= zoom2_smc911x_resources,
};

#ifdef CONFIG_WL127X_POWER
static int wl127x_gpios[] = {
	109,    /* Bluetooth Enable GPIO */
	161,    /* FM Enable GPIO */
	61,     /* BT Active LED */
};

static struct platform_device zoom2_wl127x_device = {
	.name           = "wl127x",
	.id             = -1,
	.dev.platform_data = &wl127x_gpios,
};
#endif

/* Zoom2 has Qwerty keyboard*/
static int zoom2_twl4030_keymap[] = {
	KEY(0, 0, KEY_E),
	KEY(1, 0, KEY_R),
	KEY(2, 0, KEY_T),
	KEY(3, 0, KEY_HOME),
	KEY(6, 0, KEY_I),
	KEY(7, 0, KEY_LEFTSHIFT),
	KEY(0, 1, KEY_D),
	KEY(1, 1, KEY_F),
	KEY(2, 1, KEY_G),
	KEY(3, 1, KEY_SEND),
	KEY(6, 1, KEY_K),
	KEY(7, 1, KEY_ENTER),
	KEY(0, 2, KEY_X),
	KEY(1, 2, KEY_C),
	KEY(2, 2, KEY_V),
	KEY(3, 2, KEY_END),
	KEY(6, 2, KEY_DOT),
	KEY(7, 2, KEY_CAPSLOCK),
	KEY(0, 3, KEY_Z),
	KEY(1, 3, KEY_KPPLUS),
	KEY(2, 3, KEY_B),
	KEY(3, 3, KEY_F1),
	KEY(6, 3, KEY_O),
	KEY(7, 3, KEY_SPACE),
	KEY(0, 4, KEY_W),
	KEY(1, 4, KEY_Y),
	KEY(2, 4, KEY_U),
	KEY(3, 4, KEY_F2),
	KEY(4, 4, KEY_VOLUMEUP),
	KEY(6, 4, KEY_L),
	KEY(7, 4, KEY_LEFT),
	KEY(0, 5, KEY_S),
	KEY(1, 5, KEY_H),
	KEY(2, 5, KEY_J),
	KEY(3, 5, KEY_F3),
	KEY(5, 5, KEY_VOLUMEDOWN),
	KEY(6, 5, KEY_M),
	KEY(4, 5, KEY_ENTER),
	KEY(7, 5, KEY_RIGHT),
	KEY(0, 6, KEY_Q),
	KEY(1, 6, KEY_A),
	KEY(2, 6, KEY_N),
	KEY(3, 6, KEY_BACKSPACE),
	KEY(6, 6, KEY_P),
	KEY(7, 6, KEY_UP),
	KEY(6, 7, KEY_SELECT),
	KEY(7, 7, KEY_DOWN),
	KEY(0, 7, KEY_PROG1),	/*MACRO 1 <User defined> */
	KEY(1, 7, KEY_PROG2),	/*MACRO 2 <User defined> */
	KEY(2, 7, KEY_PROG3),	/*MACRO 3 <User defined> */
	KEY(3, 7, KEY_PROG4),	/*MACRO 4 <User defined> */
	0
};

static struct twl4030_keypad_data zoom2_kp_twl4030_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap		= zoom2_twl4030_keymap,
	.keymapsize	= ARRAY_SIZE(zoom2_twl4030_keymap),
	.rep		= 1,
};

static int __init msecure_init(void)
{
	int ret = 0;

#ifdef CONFIG_RTC_DRV_TWL4030
	/* 3430ES2.0 doesn't have msecure/gpio-22 line connected to T2 */
	if (omap_type() == OMAP2_DEVICE_TYPE_GP &&
			omap_rev() < OMAP3430_REV_ES2_0) {
		void __iomem *msecure_pad_config_reg =
			omap_ctrl_base_get() + 0xA3C;
		int mux_mask = 0x04;
		u16 tmp;

		ret = gpio_request(TWL4030_MSECURE_GPIO, "msecure");
		if (ret < 0) {
			printk(KERN_ERR "msecure_init: can't"
				"reserve GPIO:%d !\n", TWL4030_MSECURE_GPIO);
			goto out;
		}
		/*
		 * TWL4030 will be in secure mode if msecure line from OMAP
		 * is low. Make msecure line high in order to change the
		 * TWL4030 RTC time and calender registers.
		 */

		tmp = __raw_readw(msecure_pad_config_reg);
		tmp &= 0xF8;	/* To enable mux mode 03/04 = GPIO_RTC */
		tmp |= mux_mask;/* To enable mux mode 03/04 = GPIO_RTC */
		__raw_writew(tmp, msecure_pad_config_reg);

		gpio_direction_output(TWL4030_MSECURE_GPIO, 1);
	}
out:
#endif
	return ret;
}

static struct omap2_mcspi_device_config zoom2_lcd_mcspi_config = {
	.turbo_mode		= 0,
	.single_channel		= 1,  /* 0: slave, 1: master */
};

static struct spi_board_info zoom2_spi_board_info[] __initdata = {
	[0] = {
		.modalias		= "zoom2_disp_spi",
		.bus_num		= 1,
		.chip_select		= 2,
		.max_speed_hz		= 375000,
		.controller_data 	= &zoom2_lcd_mcspi_config,
	},
};

#define LCD_PANEL_BACKLIGHT_GPIO 	(15 + OMAP_MAX_GPIO_LINES)
#define LCD_PANEL_ENABLE_GPIO 		(7 + OMAP_MAX_GPIO_LINES)

#define LCD_PANEL_RESET_GPIO		55
#define LCD_PANEL_QVGA_GPIO		56


#define PM_RECEIVER             TWL4030_MODULE_PM_RECEIVER
#define ENABLE_VAUX2_DEDICATED  0x09
#define ENABLE_VAUX2_DEV_GRP    0x20
#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define ENABLE_VPLL2_DEDICATED          0x05
#define ENABLE_VPLL2_DEV_GRP            0xE0
#define TWL4030_VPLL2_DEV_GRP           0x33
#define TWL4030_VPLL2_DEDICATED         0x36

#define t2_out(c, r, v) twl4030_i2c_write_u8(c, r, v)


static int zoom2_panel_enable_lcd(struct omap_display *display)
{
	return 0;
}

static void zoom2_panel_disable_lcd(struct omap_display *display)
{
}

static struct omap_dss_display_config zoom2_display_data_lcd = {
	.type = OMAP_DISPLAY_TYPE_DPI,
	.name = "lcd",
	.panel_name = "panel-zoom2",
	.u.dpi.data_lines = 24,
	.panel_enable = zoom2_panel_enable_lcd,
	.panel_disable = zoom2_panel_disable_lcd,
 };

static struct omap_dss_board_info zoom2_dss_data = {
	.num_displays = 1,
	.displays = {
		&zoom2_display_data_lcd,
	}
};

static struct platform_device zoom2_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &zoom2_dss_data,
	},
};

static struct platform_device *zoom2_devices[] __initdata = {
	&zoom2_dss_device,
	&zoom2_smc911x_device,
#ifdef CONFIG_WL127X_POWER
	&zoom2_wl127x_device,
#endif
};

static inline void __init zoom2_init_smc911x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	int eth_gpio = 0;

	eth_cs = LDP_SMC911X_CS;

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc911x\n");
		return;
	}

	zoom2_smc911x_resources[0].start = cs_mem_base + 0x0;
	zoom2_smc911x_resources[0].end   = cs_mem_base + 0xf;
	udelay(100);

	eth_gpio = LDP_SMC911X_GPIO;

	zoom2_smc911x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (gpio_request(eth_gpio, "smc911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc911x IRQ\n",
		       eth_gpio);
		return;
	}
	gpio_direction_input(eth_gpio);
}

/* Quad UART (TL16CP754C) is on Zoom2 debug board */
/* Map registers to GPMC CS3 */
static inline void __init zoom2_init_quaduart(void)
{
	int quart_cs;
	unsigned long cs_mem_base;
	int quart_gpio = 0;

	quart_cs = 3;
	if (gpmc_cs_request(quart_cs, SZ_1M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem"
				"for Quad UART(TL16CP754C)\n");
		return;
	}

	quart_gpio = 102;
	if (gpio_request(quart_gpio, "quart") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for TL16CP754C\n",
								quart_gpio);
		return;
	}
	gpio_direction_input(quart_gpio);
}

static void __init omap_zoom2_init_irq(void)
{
	omap2_init_common_hw(NULL, NULL, NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
	zoom2_init_smc911x();
}

static struct omap_lcd_config zoom2_lcd_config __initdata = {
        .ctrl_name      = "internal",
};

static struct omap_uart_config zoom2_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)),
};

static struct omap_board_config_kernel zoom2_config[] __initdata = {
	{ OMAP_TAG_UART,	&zoom2_uart_config },
        { OMAP_TAG_LCD,         &zoom2_lcd_config },
};

static int zoom2_batt_table[] = {
/* 0 C*/
30800, 29500, 28300, 27100,
26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
11600, 11200, 10800, 10400, 10000, 9630,   9280,   8950,   8620,   8310,
8020,   7730,   7460,   7200,   6950,   6710,   6470,   6250,   6040,   5830,
5640,   5450,   5260,   5090,   4920,   4760,   4600,   4450,   4310,   4170,
4040,   3910,   3790,   3670,   3550
};

static struct twl4030_bci_platform_data zoom2_bci_data = {
	.battery_tmp_tbl	= zoom2_batt_table,
	.tblsize		= ARRAY_SIZE(zoom2_batt_table),
};

static struct twl4030_usb_data zoom2_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_gpio_platform_data zoom2_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_madc_platform_data zoom2_madc_data = {
	.irq_line	= 1,
};

static struct twl4030_platform_data zoom2_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.bci		= &zoom2_bci_data,
	.madc		= &zoom2_madc_data,
	.usb		= &zoom2_usb_data,
	.gpio		= &zoom2_gpio_data,
	.keypad		= &zoom2_kp_twl4030_data,
};

static struct i2c_board_info __initdata zoom2_i2c_bus1_info[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &zoom2_twldata,
	},
};

static void synaptics_dev_init(void)
{
	/* Set the ts_gpio pin mux */
	omap_cfg_reg(R21_3430_GPIO163);

	if (gpio_request(OMAP_SYNAPTICS_GPIO, "touch") < 0) {
		printk(KERN_ERR "can't get synaptics pen down GPIO\n");
		return;
	}
	gpio_direction_input(OMAP_SYNAPTICS_GPIO);
	omap_set_gpio_debounce(OMAP_SYNAPTICS_GPIO, 1);
	omap_set_gpio_debounce_time(OMAP_SYNAPTICS_GPIO, 0xa);
}

static int synaptics_power(int power_state)
{
	/* TODO: synaptics is powered by vbatt */
	return 0;
}

static struct synaptics_i2c_rmi_platform_data synaptics_platform_data[] = {
	{
		.version	= 0x0,
		.power		= &synaptics_power,
		.flags		= SYNAPTICS_SWAP_XY,
		.irqflags	= IRQF_TRIGGER_LOW,
	}
};

static struct i2c_board_info __initdata zoom2_i2c_bus2_info[] = {
	{
		I2C_BOARD_INFO(SYNAPTICS_I2C_RMI_NAME,  0x20),
		.platform_data = &synaptics_platform_data,
		.irq = OMAP_GPIO_IRQ(OMAP_SYNAPTICS_GPIO),
	},
};

static int __init omap_i2c_init(void)
{
	omap_register_i2c_bus(1, 100, zoom2_i2c_bus1_info,
			ARRAY_SIZE(zoom2_i2c_bus1_info));
	omap_register_i2c_bus(2, 100, zoom2_i2c_bus2_info,
			ARRAY_SIZE(zoom2_i2c_bus2_info));
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static void config_wlan_gpio(void)
{
	/* WLAN PW_EN and IRQ */
	omap_cfg_reg(B24_3430_GPIO101_OUT);
	omap_cfg_reg(W21_3430_GPIO162);
}

static void config_mmc3_init(void)
{
	/* MMC3 */
	omap_cfg_reg(AF10_3430_MMC3_CLK);
	omap_cfg_reg(AC3_3430_MMC3_CMD);
	omap_cfg_reg(AE11_3430_MMC3_DAT0);
	omap_cfg_reg(AH9_3430_MMC3_DAT1);
	omap_cfg_reg(AF13_3430_MMC3_DAT2);
	omap_cfg_reg(AE13_3430_MMC3_DAT3);
}

static struct twl4030_hsmmc_info mmc[] __initdata = {
        {
                .mmc            = 1,
                .wires          = 4,
                .gpio_cd        = -EINVAL,
                .gpio_wp        = -EINVAL,
        },
        {}      /* Terminator */
};

static void __init omap_zoom2_init(void)
{
	omap_i2c_init();
	platform_add_devices(zoom2_devices, ARRAY_SIZE(zoom2_devices));
	omap_board_config = zoom2_config;
	omap_board_config_size = ARRAY_SIZE(zoom2_config);
	spi_register_board_info(zoom2_spi_board_info,
				ARRAY_SIZE(zoom2_spi_board_info));
	synaptics_dev_init();
	msecure_init();
	ldp_flash_init();
	zoom2_init_quaduart();
	omap_serial_init();
	usb_musb_init();
	twl4030_mmc_init(mmc);
	config_mmc3_init();
	config_wlan_gpio();
#if 0
	hsmmc_init();
#endif
}

static struct map_desc zoom2_io_desc[] __initdata = {
	{
		.virtual	= ZOOM2_QUART_VIRT,
		.pfn		= __phys_to_pfn(ZOOM2_QUART_PHYS),
		.length		= ZOOM2_QUART_SIZE,
		.type		= MT_DEVICE
	},
};

static void __init omap_zoom2_map_io(void)
{
	omap2_set_globals_343x();
	iotable_init(zoom2_io_desc, ARRAY_SIZE(zoom2_io_desc));
	omap2_map_common_io();
}

MACHINE_START(OMAP_ZOOM2, "OMAP ZOOM2 board")
	/* phys_io is only used for DEBUG_LL early printing.  The Zoom2's
	 * console is on an external quad UART sitting at address 0x10000000
	 */
	.phys_io	= 0x10000000,
	.io_pg_offst	= ((0xfb000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_zoom2_map_io,
	.init_irq	= omap_zoom2_init_irq,
	.init_machine	= omap_zoom2_init,
	.timer		= &omap_timer,
MACHINE_END
