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

#include "services_headers.h"
#include "pdump_km.h"

static IMG_BOOL gbInitServerRunning = IMG_FALSE;
static IMG_BOOL gbInitServerRan = IMG_FALSE;
static IMG_BOOL gbInitSuccessful = IMG_FALSE;

IMG_EXPORT
PVRSRV_ERROR PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_STATE eInitServerState, IMG_BOOL bState)
{	 

	switch(eInitServerState)
	{
		case PVRSRV_INIT_SERVER_RUNNING:
			gbInitServerRunning	= bState;
			break;
		case PVRSRV_INIT_SERVER_RAN:	
			gbInitServerRan	= bState;
			break;
		case PVRSRV_INIT_SERVER_SUCCESSFUL:	
			gbInitSuccessful = bState;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVSetInitServerState : Unknown state %lx", eInitServerState));
			return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

IMG_EXPORT
IMG_BOOL PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_STATE eInitServerState)
{						 
	IMG_BOOL	bReturnVal;
	
	switch(eInitServerState)
	{
		case PVRSRV_INIT_SERVER_RUNNING:
			bReturnVal = gbInitServerRunning;
			break;
		case PVRSRV_INIT_SERVER_RAN:	
			bReturnVal = gbInitServerRan;
			break;
		case PVRSRV_INIT_SERVER_SUCCESSFUL:	
			bReturnVal = gbInitSuccessful;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVGetInitServerState : Unknown state %lx", eInitServerState));
			bReturnVal = IMG_FALSE;
	}

	return bReturnVal;
}

static IMG_BOOL _IsSystemStatePowered(PVR_POWER_STATE eSystemPowerState)
{
	return (IMG_BOOL)(eSystemPowerState < PVRSRV_POWER_STATE_D2);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVPowerLock(IMG_UINT32	ui32CallerID,
							 IMG_BOOL	bSystemPowerEvent)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData;
	IMG_UINT32		ui32Timeout = 1000000;

#if defined(SUPPORT_LMA)
	
	ui32Timeout *= 60;
#endif 

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	do
	{
		eError = OSLockResource(&psSysData->sPowerStateChangeResource,
								ui32CallerID);
		if (eError == PVRSRV_OK)
		{
			break;
		}
		else if (ui32CallerID == ISR_ID)
		{
			

			eError = PVRSRV_ERROR_RETRY;
			break;
		}

		OSWaitus(1);
		ui32Timeout--;
	} while (ui32Timeout > 0);

	if ((eError == PVRSRV_OK) &&
		!bSystemPowerEvent &&
		!_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		
		PVRSRVPowerUnlock(ui32CallerID);
		eError = PVRSRV_ERROR_RETRY;
	}

	return eError;
}


IMG_EXPORT
IMG_VOID PVRSRVPowerUnlock(IMG_UINT32	ui32CallerID)
{
	OSUnlockResource(&gpsSysData->sPowerStateChangeResource, ui32CallerID);
}


static
PVRSRV_ERROR PVRSRVDevicePrePowerStateKM(IMG_BOOL			bAllDevices,
										 IMG_UINT32			ui32DeviceIndex,
										 PVR_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;
	PVR_POWER_STATE		eNewDevicePowerState;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	
	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
		{
			eNewDevicePowerState = (eNewPowerState == PVRSRV_POWER_Unspecified) ?
									psPowerDevice->eDefaultPowerState : eNewPowerState;
			
			if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
			{
				if (psPowerDevice->pfnPrePower != IMG_NULL)
				{
					
					eError = psPowerDevice->pfnPrePower(psPowerDevice->hDevCookie,
														eNewDevicePowerState,
														psPowerDevice->eCurrentPowerState);
					if (eError != PVRSRV_OK)
					{
						return eError;
					}
				}

				
				eError = SysDevicePrePowerState(psPowerDevice->ui32DeviceIndex,
												eNewDevicePowerState,
												psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					return eError;
				}
			}
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	return PVRSRV_OK;
}


static
PVRSRV_ERROR PVRSRVDevicePostPowerStateKM(IMG_BOOL			bAllDevices,
										  IMG_UINT32		ui32DeviceIndex,
										  PVR_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;
	PVR_POWER_STATE		eNewDevicePowerState;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	
	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (bAllDevices || (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex))
		{
			eNewDevicePowerState = (eNewPowerState == PVRSRV_POWER_Unspecified) ?
									psPowerDevice->eDefaultPowerState : eNewPowerState;

			if (psPowerDevice->eCurrentPowerState != eNewDevicePowerState)
			{
				
				eError = SysDevicePostPowerState(psPowerDevice->ui32DeviceIndex,
												 eNewDevicePowerState,
												 psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					return eError;
				}

				if (psPowerDevice->pfnPostPower != IMG_NULL)
				{
					
					eError = psPowerDevice->pfnPostPower(psPowerDevice->hDevCookie,
														 eNewDevicePowerState,
														 psPowerDevice->eCurrentPowerState);
					if (eError != PVRSRV_OK)
					{
						return eError;
					}
				}

				psPowerDevice->eCurrentPowerState = eNewDevicePowerState;
			}
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVSetDevicePowerStateCoreKM(IMG_UINT32			ui32DeviceIndex,
                                             PVR_POWER_STATE	eNewPowerState)
{
	PVRSRV_ERROR	eError;
	eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);
	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDevicePowerStateKM(IMG_UINT32			ui32DeviceIndex,
										 PVR_POWER_STATE	eNewPowerState,
										 IMG_UINT32			ui32CallerID,
										 IMG_BOOL			bRetainMutex)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	#if defined(PDUMP)
	if (eNewPowerState == PVRSRV_POWER_Unspecified)
	{
		



		eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, PVRSRV_POWER_STATE_D0);
		if(eError != PVRSRV_OK)
		{
			goto Exit;
		}

		eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, PVRSRV_POWER_STATE_D0);

		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}
		
		PDUMPSUSPEND();		
	}
	#endif 

	eError = PVRSRVDevicePrePowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);
	if(eError != PVRSRV_OK)
	{
		if (eNewPowerState == PVRSRV_POWER_Unspecified)
		{
			PDUMPRESUME();
		}
		goto Exit;
	}

	eError = PVRSRVDevicePostPowerStateKM(IMG_FALSE, ui32DeviceIndex, eNewPowerState);

	if (eNewPowerState == PVRSRV_POWER_Unspecified)
	{
		PDUMPRESUME();
	}

Exit:

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVSetDevicePowerStateKM : Transition to %d FAILED 0x%x", eNewPowerState, eError));
	}

	if (!bRetainMutex || (eError != PVRSRV_OK))
	{
		PVRSRVPowerUnlock(ui32CallerID);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSystemPrePowerStateKM(PVR_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVR_POWER_STATE		eNewDevicePowerState;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	
	eError = PVRSRVPowerLock(KERNEL_ID, IMG_TRUE);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	if (_IsSystemStatePowered(eNewPowerState) !=
		_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		if (_IsSystemStatePowered(eNewPowerState))
		{
			
			eNewDevicePowerState = PVRSRV_POWER_Unspecified;
		}
		else
		{
			eNewDevicePowerState = PVRSRV_POWER_STATE_D3;
		}

		
		eError = PVRSRVDevicePrePowerStateKM(IMG_TRUE, 0, eNewDevicePowerState);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}
	
	if (eNewPowerState != psSysData->eCurrentPowerState)
	{
		
		eError = SysSystemPrePowerState(eNewPowerState);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}

	return eError;

ErrorExit:

	PVR_DPF((PVR_DBG_ERROR,
			"PVRSRVSystemPrePowerStateKM: Transition from %d to %d FAILED 0x%x",
			psSysData->eCurrentPowerState, eNewPowerState, eError));

	
	psSysData->eFailedPowerState = eNewPowerState;

	PVRSRVPowerUnlock(KERNEL_ID);

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSystemPostPowerStateKM(PVR_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVR_POWER_STATE		eNewDevicePowerState;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		goto Exit;
	}

	if (eNewPowerState != psSysData->eCurrentPowerState)
	{
		
		eError = SysSystemPostPowerState(eNewPowerState);
		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}
	}

	if (_IsSystemStatePowered(eNewPowerState) !=
		_IsSystemStatePowered(psSysData->eCurrentPowerState))
	{
		if (_IsSystemStatePowered(eNewPowerState))
		{
			
			eNewDevicePowerState = PVRSRV_POWER_Unspecified;
		}
		else
		{
			eNewDevicePowerState = PVRSRV_POWER_STATE_D3;
		}

		
		eError = PVRSRVDevicePostPowerStateKM(IMG_TRUE, 0, eNewDevicePowerState);
		if (eError != PVRSRV_OK)
		{
			goto Exit;
		}
	}
	
	PVR_DPF((PVR_DBG_WARNING,
			"PVRSRVSystemPostPowerStateKM: System Power Transition from %d to %d OK",
			psSysData->eCurrentPowerState, eNewPowerState));

	psSysData->eCurrentPowerState = eNewPowerState;

Exit:

	PVRSRVPowerUnlock(KERNEL_ID);

	if (_IsSystemStatePowered(eNewPowerState) &&
			PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL))
	{
		


		PVRSRVCommandCompleteCallbacks();
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetPowerStateKM(PVR_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR	eError;
	SYS_DATA		*psSysData;

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = PVRSRVSystemPrePowerStateKM(eNewPowerState);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	eError = PVRSRVSystemPostPowerStateKM(eNewPowerState);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	
	psSysData->eFailedPowerState = PVRSRV_POWER_Unspecified;

	return PVRSRV_OK;

ErrorExit:

	PVR_DPF((PVR_DBG_ERROR,
			"PVRSRVSetPowerStateKM: Transition from %d to %d FAILED 0x%x",
			psSysData->eCurrentPowerState, eNewPowerState, eError));

	
	psSysData->eFailedPowerState = eNewPowerState;

	return eError;
}


PVRSRV_ERROR PVRSRVRegisterPowerDevice(IMG_UINT32					ui32DeviceIndex,
									   PFN_PRE_POWER				pfnPrePower,
									   PFN_POST_POWER				pfnPostPower,
									   PFN_PRE_CLOCKSPEED_CHANGE	pfnPreClockSpeedChange,
									   PFN_POST_CLOCKSPEED_CHANGE	pfnPostClockSpeedChange,
									   IMG_HANDLE					hDevCookie,
									   PVR_POWER_STATE				eCurrentPowerState,
									   PVR_POWER_STATE				eDefaultPowerState)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	if (pfnPrePower == IMG_NULL &&
		pfnPostPower == IMG_NULL)
	{
		return PVRSRVRemovePowerDevice(ui32DeviceIndex);
	}

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
						 sizeof(PVRSRV_POWER_DEV),
						 (IMG_VOID **)&psPowerDevice, IMG_NULL);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterPowerDevice: Failed to alloc PVRSRV_POWER_DEV"));
		return eError;
	}

	
	psPowerDevice->pfnPrePower = pfnPrePower;
	psPowerDevice->pfnPostPower = pfnPostPower;
	psPowerDevice->pfnPreClockSpeedChange = pfnPreClockSpeedChange;
	psPowerDevice->pfnPostClockSpeedChange = pfnPostClockSpeedChange;
	psPowerDevice->hDevCookie = hDevCookie;
	psPowerDevice->ui32DeviceIndex = ui32DeviceIndex;
	psPowerDevice->eCurrentPowerState = eCurrentPowerState;
	psPowerDevice->eDefaultPowerState = eDefaultPowerState;

	
	psPowerDevice->psNext = psSysData->psPowerDeviceList;
	psSysData->psPowerDeviceList = psPowerDevice;

	return (PVRSRV_OK);
}


PVRSRV_ERROR PVRSRVRemovePowerDevice (IMG_UINT32 ui32DeviceIndex)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psCurrent, *psPrevious;

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	
	psCurrent = psSysData->psPowerDeviceList;
	psPrevious = IMG_NULL;

	while (psCurrent)
	{
		if (psCurrent->ui32DeviceIndex == ui32DeviceIndex)
		{
			
			if (psPrevious)
			{
				psPrevious->psNext = psCurrent->psNext;
			}
			else
			{
				
				psSysData->psPowerDeviceList = psCurrent->psNext;
			}

			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_POWER_DEV), psCurrent, IMG_NULL);
			
			break;
		}
		else
		{
			psPrevious = psCurrent;
			psCurrent = psCurrent->psNext;
		}
	}

	return (PVRSRV_OK);
}


IMG_EXPORT
IMG_BOOL PVRSRVIsDevicePowered(IMG_UINT32 ui32DeviceIndex)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return IMG_FALSE;
	}

	if (OSIsResourceLocked(&psSysData->sPowerStateChangeResource, KERNEL_ID) ||
		OSIsResourceLocked(&psSysData->sPowerStateChangeResource, ISR_ID))
	{
		return IMG_FALSE;
	}

	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (psPowerDevice->ui32DeviceIndex == ui32DeviceIndex)
		{
			return (IMG_BOOL)(psPowerDevice->eCurrentPowerState == PVRSRV_POWER_STATE_D0);
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	
	return IMG_FALSE;
}


PVRSRV_ERROR PVRSRVDevicePreClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
											 IMG_BOOL	bIdleDevice,
											 IMG_VOID	*pvInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	SysAcquireData(&psSysData);

	if (bIdleDevice)
	{
		
		eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,	"PVRSRVDevicePreClockSpeedChange : failed to acquire lock, error:0x%lx", eError));
			return eError;
		}
	}
	
	
	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex)
		{
			if (psPowerDevice->pfnPreClockSpeedChange)
			{
				eError = psPowerDevice->pfnPreClockSpeedChange(psPowerDevice->hDevCookie,
															   bIdleDevice,
															   psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,
							"PVRSRVDevicePreClockSpeedChange : Device %lu failed, error:0x%lx",
							ui32DeviceIndex, eError));
				}
			}
		}
		
		psPowerDevice = psPowerDevice->psNext;
	}
	
	return eError;
}


IMG_VOID PVRSRVDevicePostClockSpeedChange(IMG_UINT32	ui32DeviceIndex,
										  IMG_BOOL		bIdleDevice,
										  IMG_VOID		*pvInfo)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;

	PVR_UNREFERENCED_PARAMETER(pvInfo);

	SysAcquireData(&psSysData);

	
	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (ui32DeviceIndex == psPowerDevice->ui32DeviceIndex)
		{
			if (psPowerDevice->pfnPostClockSpeedChange)
			{
				eError = psPowerDevice->pfnPostClockSpeedChange(psPowerDevice->hDevCookie,
																bIdleDevice,
																psPowerDevice->eCurrentPowerState);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,
							"PVRSRVDevicePostClockSpeedChange : Device %lu failed, error:0x%lx",
							ui32DeviceIndex, eError));
				}
			}
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	if (bIdleDevice)
	{
		
		PVRSRVPowerUnlock(KERNEL_ID);
	}
}

