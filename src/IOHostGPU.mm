/*
 This file is part of Darling.

 Copyright (C) 2026 Lubos Dolezel

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

#include "IOHostGPU.h"
#import <Foundation/NSString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSNumber.h>
#include <sys/sysctl.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The first line of a sysfs file, or an empty string
static std::string readSysfs(const std::string& path)
{
	FILE* file = fopen(path.c_str(), "r");
	if (!file)
		return std::string();

	char line[128];
	std::string result;
	if (fgets(line, sizeof(line), file))
	{
		result = line;
		while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
			result.pop_back();
	}
	fclose(file);
	return result;
}

// A PCI id as the 4 little-endian bytes that IOPCIDevice's "vendor-id" and "device-id" are
static NSData* pciID(uint32_t value)
{
	uint8_t bytes[4] = { uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24) };
	return [NSData dataWithBytes: bytes length: sizeof(bytes)];
}

// "cardN" and nothing else: the connectors of a device are "cardN-<connector>" in the same directory
static bool isCardName(const char* name)
{
	if (strncmp(name, "card", 4) != 0 || name[4] == '\0')
		return false;
	for (const char* c = name + 4; *c; c++)
	{
		if (*c < '0' || *c > '9')
			return false;
	}
	return true;
}

static uint64_t systemMemoryShare()
{
	uint64_t memsize = 0;
	size_t length = sizeof(memsize);
	if (sysctlbyname("hw.memsize", &memsize, &length, NULL, 0) != 0)
		return 0;
	return memsize / 2;
}

IOHostGPU::IOHostGPU(NSDictionary* properties)
{
	m_properties = [properties retain];
}

NSDictionary* IOHostGPU::getProperties()
{
	return m_properties;
}

void IOHostGPU::registerSelf(ServiceRegistry* targetServiceRegistry, const char* sysfs_drm_directory)
{
	DIR* directory = opendir(sysfs_drm_directory);
	if (!directory)
		return;

	std::vector<std::string> cards;
	while (struct dirent* entry = readdir(directory))
	{
		if (isCardName(entry->d_name))
			cards.push_back(entry->d_name);
	}
	closedir(directory);

	for (const std::string& card : cards)
	{
		std::string device = std::string(sysfs_drm_directory) + "/" + card + "/device/";
		std::string vendor = readSysfs(device + "vendor");
		std::string deviceID = readSysfs(device + "device");
		std::string vram = readSysfs(device + "mem_info_vram_total");

		// the memory of the GPU itself if the driver tells, otherwise a share of system memory
		// (our policy, see the header)
		uint64_t bytes = vram.empty() ? 0 : strtoull(vram.c_str(), nullptr, 10);
		if (bytes == 0)
			bytes = systemMemoryShare();

		NSMutableDictionary* accelerator = [NSMutableDictionary dictionary];
		accelerator[@"IOMatchCategory"] = @"IOAccelerator";
		if (!vendor.empty())
			accelerator[@"vendor-id"] = pciID((uint32_t) strtoul(vendor.c_str(), nullptr, 16));
		if (!deviceID.empty())
			accelerator[@"device-id"] = pciID((uint32_t) strtoul(deviceID.c_str(), nullptr, 16));

		if (bytes != 0)
			accelerator[@"VRAM,totalMB"] = @(bytes / (1024 * 1024));

		IOAccelerator* acc = new IOAccelerator(accelerator);
		targetServiceRegistry->registerService(acc);
		acc->registerInPlane(kIOServicePlane, "IOAccelerator", IORegistryEntry::root());
	}
}

const char* IOAccelerator::className() const
{
	return "IOAccelerator";
}

NSDictionary* IOAccelerator::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOAccelerator",
		@"IOClass": @"IOAccelerator",
	};
}
