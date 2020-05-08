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
#include "IOSurfaceRoot.h"
#import <Foundation/NSString.h>

void IOSurfaceRoot::registerSelf(ServiceRegistry* targetServiceRegistry)
{
	static IOSurfaceRoot instance;
	
	targetServiceRegistry->registerService(&instance);
	instance.registerInPlane(kIOServicePlane, "IOSurfaceRoot", IORegistryEntry::root());
}

const char* IOSurfaceRoot::className() const
{
	return "IOSurfaceRoot";
}

NSDictionary* IOSurfaceRoot::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOResources",
		@"IOClass": @"IOSurfaceRoot",
		@"IOMatchCategory": @"IOSurfaceRoot",
		@"IOResourceMatch": @"IOBSD",
	};
}

IOExternalMethod *IOSurfaceRoot::getExternalMethodForIndex(UInt32 index)
{
	static const IOExternalMethod methods[] = {
		[kIOSurfaceMethodCreate] = {
			.func = IOMethod(&IOSurfaceRoot::createSurface),
			.flags = kIOUCStructIStructO,
			.count0 = kIOUCVariableStructureSize,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodRelease] = {
			.func = IOMethod(&IOSurfaceRoot::releaseSurface),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodLock] = {
			.func = IOMethod(&IOSurfaceRoot::lockSurface),
			.flags = kIOUCStructIStructO,
			.count0 = 8,
			.count1 = 0,
		},
		[kIOSurfaceMethodUnlock] = {
			.func = IOMethod(&IOSurfaceRoot::unlockSurface),
			.flags = kIOUCStructIStructO,
			.count0 = 8,
			.count1 = 0,
		},
		[kIOSurfaceMethodLookupByID] = {
			.func = IOMethod(&IOSurfaceRoot::lookupById),
			.flags = kIOUCScalarIStructO,
			.count0 = 1,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodIncrementUseCount] = {
			.func = IOMethod(&IOSurfaceRoot::incrementSurfaceUseCount),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodDecrementUseCount] = {
			.func = IOMethod(&IOSurfaceRoot::decrementSurfaceUseCount),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodCreateByMachPort] = {
			.func = IOMethod(&IOSurfaceRoot::lookupByMachPort),
			.flags = kIOUCScalarIStructO,
			.count0 = 1,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodCreateMachPort] = {
			.func = IOMethod(&IOSurfaceRoot::getSurfaceMachPort),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 1,
		},
	};

	if (index >= sizeof(methods)/sizeof(IOExternalMethod))
		return nullptr;

	static IOExternalMethod method = methods[index];
	method.object = this;
	return &method;
}

IOReturn IOSurfaceRoot::createSurface(const void* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::releaseSurface(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::unlockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lookupById(IOSurfaceID surfaceID, void* outputData, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::incrementSurfaceUseCount(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::decrementSurfaceUseCount(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lookupByMachPort(mach_port_t remoteMachPort, void* outputData, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::getSurfaceMachPort(IOSurfaceID surfaceID, mach_port_t* remoteMachPort)
{
	return KERN_NOT_SUPPORTED;
}
