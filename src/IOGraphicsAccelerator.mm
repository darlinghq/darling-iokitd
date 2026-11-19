/*
 This file is part of Darling.

 Copyright (C) 2026 Darling developers

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

#include "IOGraphicsAccelerator.h"
#include "IOGraphicsAcceleratorIds.h"
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#include <os/log.h>
#include <cstring>

static NSData* dataForWord(uint32_t word)
{
	return [NSData dataWithBytes: &word length: sizeof(word)];
}

IOPCIDevice::IOPCIDevice(uint32_t vendor, uint32_t device)
: m_vendor(vendor), m_device(device)
{
}

const char* IOPCIDevice::className() const
{
	return "IOPCIDevice";
}

NSDictionary* IOPCIDevice::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOPCIDevice",
		@"IOClass": @"IOPCIDevice",
	};
}

NSDictionary* IOPCIDevice::getProperties()
{
	// macOS publishes both IDs as four-byte little-endian data; Dawn reads them
	// from the accelerator's parent entry with IORegistryEntrySearchCFProperty.
	return @{
		@"vendor-id": dataForWord(m_vendor),
		@"device-id": dataForWord(m_device),
	};
}

const char* IOGraphicsAccelerator::className() const
{
	return "IOGraphicsAccelerator2";
}

bool IOGraphicsAccelerator::conformsTo(const char* className)
{
	if (std::strcmp(className, "IOAccelerator") == 0
	    || std::strcmp(className, "IOGraphicsAccelerator") == 0)
		return true;
	return IOService::conformsTo(className);
}

NSDictionary* IOGraphicsAccelerator::matchingDictionary()
{
	return @{
		@"IOProviderClass": @[ @"IOAccelerator", @"IOGraphicsAccelerator2" ],
		@"IOClass": @"IOGraphicsAccelerator2",
	};
}

void IOGraphicsAccelerator::registerSelf(ServiceRegistry* targetServiceRegistry)
{
	static bool registered = false;

	if (registered)
		return;
	registered = true;

	// An unknown identity publishes nothing. Dawn trusts any entry it finds for
	// -[MTLDevice registryID] and reads missing or zero IDs as vendor 0; with no
	// entry it falls back to the Metal device name, as before this entry existed.
	IOGraphicsAcceleratorIds::PciId ids = IOGraphicsAcceleratorIds::acceleratorIds();
	if (ids.vendor == 0 && ids.device == 0)
		return;

	IOGraphicsAccelerator* accelerator = new IOGraphicsAccelerator;
	IOPCIDevice* pciDevice = new IOPCIDevice(ids.vendor, ids.device);

	pciDevice->registerInPlane(kIOServicePlane, "GFX0@0", IORegistryEntry::root());
	targetServiceRegistry->registerService(pciDevice);

	if (!accelerator->assignId(kIOGraphicsAcceleratorRegistryID))
		os_log_error(OS_LOG_DEFAULT, "iokitd: registry ID 0x%llx is already in use",
		             (unsigned long long) kIOGraphicsAcceleratorRegistryID);

	accelerator->registerInPlane(kIOServicePlane, "IOAccelerator", pciDevice);
	targetServiceRegistry->registerService(accelerator);
}
