/*
 * Copyright (C) 2007-2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */


#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/cpcap.h>

static int cpcap_chgr_probe(struct platform_device *pdev);
static int cpcap_chgr_remove(struct platform_device *pdev);

static struct platform_driver cpcap_chgr_driver = {
	.probe		= cpcap_chgr_probe,
	.remove		= cpcap_chgr_remove,
	.driver		= {
		.name	= "cpcap_charger",
		.owner	= THIS_MODULE,
	},
};

static int cpcap_chgr_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct cpcap_device *cpcap;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	cpcap = pdev->dev.platform_data;
	platform_set_drvdata(pdev, cpcap);
	cpcap_batt_set_ac_prop(cpcap, 1);
	return ret;
}

static int cpcap_chgr_remove(struct platform_device *pdev)
{
	struct cpcap_device *cpcap = platform_get_drvdata(pdev);
	cpcap_batt_set_ac_prop(cpcap, 0);

	return 0;
}

static int __init cpcap_chgr_init(void)
{
	return platform_driver_register(&cpcap_chgr_driver);
}
module_init(cpcap_chgr_init);

static void __exit cpcap_chgr_exit(void)
{
	platform_driver_unregister(&cpcap_chgr_driver);
}
module_exit(cpcap_chgr_exit);

MODULE_ALIAS("platform:cpcap_chgr");
MODULE_DESCRIPTION("CPCAP CHARGER driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
