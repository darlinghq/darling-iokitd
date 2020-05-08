/*
 This file is part of Darling.

 Copyright (C) 2020 Lubos Dolezel

 Darling is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Darling is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef IOKITD_IOSURFACEROOT_H
#define IOKITD_IOSURFACEROOT_H

#include "IOUserClient.h"
#include <IOSurface/IOSurfacePriv.h>
#include "ServiceRegistry.h"

class IOSurfaceRoot : public IOUserClient
{
public:
	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	IOExternalMethod *getExternalMethodForIndex(UInt32 index) override;
	static void registerSelf(ServiceRegistry* targetServiceRegistry);
private:
	IOReturn createSurface(const void* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount);
	IOReturn releaseSurface(IOSurfaceID surfaceID);
	IOReturn lockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount);
	IOReturn unlockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount);
	IOReturn lookupById(IOSurfaceID surfaceID, void* outputData, IOByteCount* outputCount);
	IOReturn incrementSurfaceUseCount(IOSurfaceID surfaceID);
	IOReturn decrementSurfaceUseCount(IOSurfaceID surfaceID);
	IOReturn lookupByMachPort(mach_port_t remoteMachPort, void* outputData, IOByteCount* outputCount);
	IOReturn getSurfaceMachPort(IOSurfaceID surfaceID, mach_port_t* remoteMachPort);
};

#endif
