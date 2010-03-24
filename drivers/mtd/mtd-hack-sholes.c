/*
 * This modules maps additional mtd partions of the Motorola Milestone
 *
 * Copyright (C) 2010 Janne Grunau
 * Copyright (C) 2010 Mike Baker
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define mtd_hack_part(pname, psize)	.name       = pname,              \
					.size       = psize * 1024,       \
					.offset     = MTDPART_OFS_APPEND, \
					.mask_flags = MTD_WRITEABLE,

#define mtd_hack_partw(pname, psize)     .name       = pname,              \
					.size       = psize * 1024,       \
					.offset     = MTDPART_OFS_APPEND, \


/* MTD partition layout:
 *
 * mtdparts=omap2-nand.0:
 * 	128k(mbmloader.img),
 * 	640k(mbm.img),
 * 	640k(mbmbackup.img),
 * 	384k(cdt.bin),
 * 	1536k(pds),
 * 	384k(lbl),
 * 	384k(lbl_backup),
 * 	1536k(sp),
 * 	640k(logo.bin),
 * 	384k(misc),
 * 	3584k(boot),
 * 	4608k(recovery),
 * 	143744k(system),
 * 	94848k(cache),
 * 	268032k(userdata),
 * 	2048k(kpanic),
 * 	768k(rsv)
 *
 */


struct mtd_partition part[] = {
	{	mtd_hack_part("h_mbmloader", 128)},
	{	mtd_hack_part("h_mbm",       640)},
	{	mtd_hack_partw("h_mbmbackup", 640)},
        {	mtd_hack_part("h_cdt",       384)},
        {	mtd_hack_part("h_pds",       1536)},
        {	mtd_hack_part("h_lbl",       384)},
        {	mtd_hack_part("h_lblbackup", 384)},
        {	mtd_hack_part("h_sp",        1536)},
        {	mtd_hack_part("h_logo",      640)},
        {	mtd_hack_part("h_misc",      384)},
	{	mtd_hack_part("h_boot",      3584)},
	{       mtd_hack_part("h_recovery",  4608)},
	{       mtd_hack_part("h_system",    143744)},
	{       mtd_hack_part("h_cache",     94848)},
	{       mtd_hack_part("h_userdata",  268032)},
	{       mtd_hack_part("h_kpanic",    2048)},
	{       mtd_hack_part("h_rsv",       768)},
};

static int create_missing_flash_parts(struct device *dev, void *data)
{

	struct mtd_info *mtd = NULL;

	printk(KERN_INFO "mtd-hack: device %s\n", dev->init_name);

	mtd = dev_get_drvdata(dev);

	if (!mtd)
		return -1;

        printk(KERN_INFO "mtd-hack: mtd name %s, type %d, size %llu\n",
                mtd->name, mtd->type, mtd->size);

        /*
        if (mtd->read) {
                size_t ret = 0;
                u_char buf[520];
                int i;
//  int (*read) (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
                mtd->read(mtd, 7995392, 512, &ret, buf);

                printk(KERN_INFO "flash contents: ");
                for (i=0; i < ret; i++)
                        printk(KERN_INFO "0x%x ", buf[i]);
        }
        */

	add_mtd_partitions(mtd, part, 17);

	return 0;
}

static int __init mtd_init(void)
{
	struct device_driver *devdrv;
	int err = 0;

	//	struct device_driver *driver_find(const char *name, struct bus_type *bus);
	devdrv = driver_find("omap2-nand", &platform_bus_type);

	printk(KERN_INFO "mtd-hack: found driver %s modname %s\n", devdrv->name, devdrv->mod_name);
	//	int driver_for_each_device(struct device_driver *drv, struct device *start,
	//	                           void *data, int (*fn)(struct device *, void *))
	err = driver_for_each_device(devdrv, NULL, NULL, create_missing_flash_parts);


	printk(KERN_INFO "mtd hack loaded");

	return 0;
}
 
module_init(mtd_init);

MODULE_LICENSE("GPL");
