/*
 * omap iommu wrapper for TI's OMAP3430 Camera ISP
 *
 * Copyright (C) 2008--2009 Nokia.
 *
 * Contributors:
 *	Hiroshi Doyu <hiroshi.doyu@nokia.com>
 *	Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/module.h>

#include "ispmmu.h"
#include "isp.h"

#include <mach/iommu.h>
#include <mach/iovmm.h>

#define IOMMU_FLAG (IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_8)

static struct iommu *isp_iommu;

dma_addr_t ispmmu_vmalloc(size_t bytes)
{
	return (dma_addr_t)iommu_vmalloc(isp_iommu, 0, bytes, IOMMU_FLAG);
}

void ispmmu_vfree(const dma_addr_t da)
{
	iommu_vfree(isp_iommu, (u32)da);
}

dma_addr_t ispmmu_kmap(u32 pa, int size)
{
	void *da;

	da = (void *)iommu_kmap(isp_iommu, 0, pa, size, IOMMU_FLAG);
	if (IS_ERR(da))
		return PTR_ERR(da);

	return (dma_addr_t)da;
}

void ispmmu_kunmap(dma_addr_t da)
{
	iommu_kunmap(isp_iommu, (u32)da);
}

dma_addr_t ispmmu_vmap(const struct scatterlist *sglist,
		       int sglen)
{
	int err;
	void *da;
	struct sg_table *sgt;
	unsigned int i;
	struct scatterlist *sg, *src = (struct scatterlist *)sglist;

	/*
	 * convert isp sglist to iommu sgt
	 * FIXME: should be fixed in the upper layer?
	 */
	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return -ENOMEM;
	err = sg_alloc_table(sgt, sglen, GFP_KERNEL);
	if (err)
		goto err_sg_alloc;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		sg_set_buf(sg, phys_to_virt(sg_dma_address(src + i)),
			   sg_dma_len(src + i));

	da = (void *)iommu_vmap(isp_iommu, 0, sgt, IOMMU_FLAG);
	if (IS_ERR(da))
		goto err_vmap;

	return (dma_addr_t)da;

err_vmap:
	sg_free_table(sgt);
err_sg_alloc:
	kfree(sgt);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(ispmmu_vmap);

void ispmmu_vunmap(dma_addr_t da)
{
	struct sg_table *sgt;

	sgt = iommu_vunmap(isp_iommu, (u32)da);
	if (!sgt)
		return;
	sg_free_table(sgt);
	kfree(sgt);
}
EXPORT_SYMBOL_GPL(ispmmu_vunmap);

void ispmmu_save_context(void)
{
	if (isp_iommu)
		iommu_save_ctx(isp_iommu);
}

void ispmmu_restore_context(void)
{
	if (isp_iommu)
		iommu_restore_ctx(isp_iommu);
}

int __init ispmmu_init(void)
{
	int err = 0;

	isp_get();
	isp_iommu = iommu_get("isp");
	if (IS_ERR(isp_iommu)) {
		err = PTR_ERR(isp_iommu);
		isp_iommu = NULL;
	}
	isp_put();

	return err;
}

void ispmmu_cleanup(void)
{
	isp_get();
	if (isp_iommu)
		iommu_put(isp_iommu);
	isp_put();
	isp_iommu = NULL;
}
