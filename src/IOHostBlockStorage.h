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

#ifndef IOKITD_IOHOSTBLOCKSTORAGE_H
#define IOKITD_IOHOSTBLOCKSTORAGE_H

#include "IOService.h"
#include "ServiceRegistry.h"
#include <string>

// The disks of the host, as the service applications that report disk activity look for:
// IOServiceMatching("IOBlockStorageDriver"), one per whole disk the host has (partitions,
// loop, RAM and device-mapper devices are not disks). The "Statistics" property is the
// dictionary with the number of read and write operations and the bytes read and written
// since boot, as the host counts them for the disk (/sys/block/<disk>/stat); it is read each
// time the properties are asked for, so that it is current.
class IOBlockStorageDriver : public IOService
{
public:
	IOBlockStorageDriver(const std::string& disk) : m_disk(disk) {}

	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	NSDictionary* getProperties() override;

	// Creates the services for the disks found in sysfs_block_directory
	// (the host's /sys is visible below /Volumes/SystemRoot)
	static void registerSelf(ServiceRegistry* targetServiceRegistry, const char* sysfs_block_directory = "/Volumes/SystemRoot/sys/block");

	// Whether the entry of /sys/block with this name is a whole disk of a kind that is a disk on macOS
	static bool isDiskName(const char* name);
protected:
	std::string m_disk;
	std::string m_statFile;
	NSDictionary* m_properties = nil;
};

#endif
