/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef _MMU_H_
#define _MMU_H_

#include "sgxinfokm.h"

PVRSRV_ERROR
MMU_Initialise (PVRSRV_DEVICE_NODE *psDeviceNode, MMU_CONTEXT **ppsMMUContext, IMG_DEV_PHYADDR *psPDDevPAddr);

IMG_VOID
MMU_Finalise (MMU_CONTEXT *psMMUContext);


IMG_VOID
MMU_InsertHeap(MMU_CONTEXT *psMMUContext, MMU_HEAP *psMMUHeap);

MMU_HEAP *
MMU_Create (MMU_CONTEXT *psMMUContext,
			DEV_ARENA_DESCRIPTOR *psDevArena,
			RA_ARENA **ppsVMArena);

IMG_VOID
MMU_Delete (MMU_HEAP *pMMU);

IMG_BOOL
MMU_Alloc (MMU_HEAP *pMMU,
           IMG_SIZE_T uSize,
           IMG_SIZE_T *pActualSize,
           IMG_UINT32 uFlags,
		   IMG_UINT32 uDevVAddrAlignment,
           IMG_DEV_VIRTADDR *pDevVAddr);

IMG_VOID
MMU_Free (MMU_HEAP *pMMU,
          IMG_DEV_VIRTADDR DevVAddr,
		  IMG_UINT32 ui32Size);

IMG_VOID 
MMU_Enable (MMU_HEAP *pMMU);

IMG_VOID 
MMU_Disable (MMU_HEAP *pMMU);

IMG_VOID
MMU_MapPages (MMU_HEAP *pMMU,
			  IMG_DEV_VIRTADDR devVAddr,
			  IMG_SYS_PHYADDR SysPAddr,
			  IMG_SIZE_T uSize,
			  IMG_UINT32 ui32MemFlags,
			  IMG_HANDLE hUniqueTag);

IMG_VOID
MMU_MapShadow (MMU_HEAP          * pMMU,
               IMG_DEV_VIRTADDR    MapBaseDevVAddr,
               IMG_SIZE_T          uSize, 
               IMG_CPU_VIRTADDR    CpuVAddr,
               IMG_HANDLE          hOSMemHandle,
               IMG_DEV_VIRTADDR  * pDevVAddr,
               IMG_UINT32          ui32MemFlags,
               IMG_HANDLE          hUniqueTag);

IMG_VOID
MMU_UnmapPages (MMU_HEAP *pMMU,
             IMG_DEV_VIRTADDR dev_vaddr,
             IMG_UINT32 ui32PageCount,
             IMG_HANDLE hUniqueTag);

IMG_VOID
MMU_MapScatter (MMU_HEAP *pMMU,
				IMG_DEV_VIRTADDR DevVAddr,
				IMG_SYS_PHYADDR *psSysAddr,
				IMG_SIZE_T uSize,
				IMG_UINT32 ui32MemFlags,
				IMG_HANDLE hUniqueTag);


IMG_DEV_PHYADDR
MMU_GetPhysPageAddr(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR sDevVPageAddr);


IMG_DEV_PHYADDR
MMU_GetPDDevPAddr(MMU_CONTEXT *pMMUContext);


#ifdef SUPPORT_SGX_MMU_BYPASS
IMG_VOID
EnableHostAccess (MMU_CONTEXT *psMMUContext);


IMG_VOID
DisableHostAccess (MMU_CONTEXT *psMMUContext);
#endif

IMG_VOID MMU_InvalidateDirectoryCache(PVRSRV_SGXDEV_INFO *psDevInfo);

PVRSRV_ERROR MMU_BIFResetPDAlloc(PVRSRV_SGXDEV_INFO *psDevInfo);

IMG_VOID MMU_BIFResetPDFree(PVRSRV_SGXDEV_INFO *psDevInfo);

#endif
