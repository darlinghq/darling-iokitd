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

#include "IOHostBlockStorage.h"
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSDictionary.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// kIOBlockStorageDriverStatisticsKey and the keys of its dictionary (IOBlockStorageDriver.h)
static NSString* const StatisticsKey = @"Statistics";
static NSString* const ReadsKey = @"Operations (Read)";
static NSString* const WritesKey = @"Operations (Write)";
static NSString* const BytesReadKey = @"Bytes (Read)";
static NSString* const BytesWrittenKey = @"Bytes (Write)";

// The sector size of the counters in /sys/block/<disk>/stat, whatever the sector size of the disk
static const uint64_t kStatSectorSize = 512;

static bool hasPrefix(const char* name, const char* prefix)
{
	return strncmp(name, prefix, strlen(prefix)) == 0;
}

bool IOBlockStorageDriver::isDiskName(const char* name)
{
	// the kinds of disks of Linux that a Mac has counterparts of: SCSI/SATA/USB, NVMe, virtio, Xen, MMC
	// and old IDE disks. Everything else (loop, ram, zram, md, dm-*, nbd, sr/CD-ROM, floppies) is not.
	static const char* const prefixes[] = { "sd", "nvme", "vd", "xvd", "mmcblk", "hd" };
	for (const char* prefix : prefixes)
	{
		if (hasPrefix(name, prefix))
			return true;
	}
	return false;
}

// "1 2 3 ..." of /sys/block/<disk>/stat: field 0 is the number of reads completed, 2 the sectors
// read, 4 the writes completed and 6 the sectors written
static bool readDiskStatistics(const std::string& path, uint64_t (&fields)[7])
{
	FILE* file = fopen(path.c_str(), "r");
	if (!file)
		return false;

	unsigned long long values[7];
	int count = fscanf(file, "%llu %llu %llu %llu %llu %llu %llu",
		&values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6]);
	fclose(file);
	if (count != 7)
		return false;

	for (int i = 0; i < 7; i++)
		fields[i] = values[i];
	return true;
}

const char* IOBlockStorageDriver::className() const
{
	return "IOBlockStorageDriver";
}

NSDictionary* IOBlockStorageDriver::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOBlockStorageDriver",
		@"IOClass": @"IOBlockStorageDriver",
	};
}

NSDictionary* IOBlockStorageDriver::getProperties()
{
	uint64_t fields[7] = { 0 };
	NSMutableDictionary* statistics = [NSMutableDictionary dictionary];

	if (readDiskStatistics(m_statFile, fields))
	{
		statistics[ReadsKey] = @(fields[0]);
		statistics[BytesReadKey] = @(fields[2] * kStatSectorSize);
		statistics[WritesKey] = @(fields[4]);
		statistics[BytesWrittenKey] = @(fields[6] * kStatSectorSize);
	}

	NSDictionary* properties = @{
		@"IOMatchCategory": @"IOBlockStorageDriver",
		StatisticsKey: statistics,
	};
	[m_properties release];
	m_properties = [properties retain];
	return m_properties;
}

void IOBlockStorageDriver::registerSelf(ServiceRegistry* targetServiceRegistry, const char* sysfs_block_directory)
{
	DIR* directory = opendir(sysfs_block_directory);
	if (!directory)
		return;

	std::vector<std::string> disks;
	while (struct dirent* entry = readdir(directory))
	{
		if (isDiskName(entry->d_name))
			disks.push_back(entry->d_name);
	}
	closedir(directory);

	for (const std::string& disk : disks)
	{
		IOBlockStorageDriver* driver = new IOBlockStorageDriver(disk);
		driver->m_statFile = std::string(sysfs_block_directory) + "/" + disk + "/stat";
		targetServiceRegistry->registerService(driver);
		driver->registerInPlane(kIOServicePlane, "IOBlockStorageDriver", IORegistryEntry::root());
	}
}
