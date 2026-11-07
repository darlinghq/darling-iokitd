/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Developers

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

extern "C" {
#include "powermanagementServer.h"
}
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSPropertyList.h>
#include <fstream>
#include <string>
#include <cstring>
#include <dirent.h>
#include <mach/mach.h>
#include <vector>
#include <IOKit/ps/IOPSKeys.h>

// Batteries and AC adapters as the Linux host reports them (see the kernel's
// sysfs-class-power ABI). The guest has no /sys; the host's is visible
// under the system root.
static const char* const kPowerSupplyDir = "/Volumes/SystemRoot/sys/class/power_supply";

static bool readSysfs(const std::string& dir, const char* name, std::string& value)
{
	std::ifstream file(dir + "/" + name);
	if (!file || !std::getline(file, value))
		return false;
	return true;
}

static bool readSysfsLong(const std::string& dir, const char* name, long long& value)
{
	std::string text;
	if (!readSysfs(dir, name, text))
		return false;
	char* end = nullptr;
	value = strtoll(text.c_str(), &end, 10);
	return end != text.c_str();
}

static std::vector<std::string> powerSupplies(const char* wantedType)
{
	std::vector<std::string> result;
	DIR* d = opendir(kPowerSupplyDir);
	if (!d)
		return result;

	while (struct dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		std::string dir = std::string(kPowerSupplyDir) + "/" + e->d_name;
		std::string type;
		if (readSysfs(dir, "type", type) && type == wantedType)
			result.push_back(dir);
	}
	closedir(d);
	return result;
}

// Returns an autoreleased array
static NSArray* internalBatteries()
{
	bool onAC = false;
	for (const std::string& ac : powerSupplies("Mains"))
	{
		long long online;
		if (readSysfsLong(ac, "online", online) && online)
			onAC = true;
	}

	NSMutableArray* sources = [NSMutableArray array];
	for (const std::string& dir : powerSupplies("Battery"))
	{
		long long present = 1, capacity, energyNow, energyFull, powerNow, voltage;
		std::string status;
		readSysfsLong(dir, "present", present);
		if (!present || !readSysfs(dir, "status", status) || !readSysfsLong(dir, "capacity", capacity))
			continue;

		NSMutableDictionary* ps = [NSMutableDictionary dictionary];
		ps[@kIOPSNameKey] = [NSString stringWithFormat: @"InternalBattery-%lu", (unsigned long) [sources count]];
		ps[@kIOPSTypeKey] = @kIOPSInternalBatteryType;
		ps[@kIOPSTransportTypeKey] = @kIOPSInternalType;
		ps[@kIOPSPowerSourceIDKey] = @((int) [sources count]);
		ps[@kIOPSIsPresentKey] = @YES;
		ps[@kIOPSCurrentCapacityKey] = @((int) capacity);
		// "capacity" is a percentage
		ps[@kIOPSMaxCapacityKey] = @100;
		ps[@kIOPSPowerSourceStateKey] = onAC ? @kIOPSACPowerValue : @kIOPSBatteryPowerValue;
		ps[@kIOPSIsChargingKey] = @(status == "Charging");
		ps[@kIOPSIsChargedKey] = @(status == "Full");

		if (readSysfsLong(dir, "voltage_now", voltage))
			ps[@kIOPSVoltageKey] = @((int) (voltage / 1000)); // uV -> mV

		// Remaining time in minutes from the energy and power draw reported by the host; -1 when unknown
		long long minutes = -1;
		if (readSysfsLong(dir, "power_now", powerNow) && powerNow > 0)
		{
			if (status == "Discharging" && readSysfsLong(dir, "energy_now", energyNow))
				minutes = energyNow * 60 / powerNow;
			else if (status == "Charging" && readSysfsLong(dir, "energy_now", energyNow) && readSysfsLong(dir, "energy_full", energyFull))
				minutes = (energyFull - energyNow) * 60 / powerNow;
		}
		if (status == "Charging")
			ps[@kIOPSTimeToFullChargeKey] = @((int) minutes);
		else
			ps[@kIOPSTimeToEmptyKey] = @((int) minutes);

		[sources addObject: ps];
	}
	return sources;
}

kern_return_t _io_ps_copy_powersources_info
(
	mach_port_t server,
	audit_token_t token,
	int pstype,
	vm_offset_t *powersources,
	mach_msg_type_number_t *powersourcesCnt,
	int *return_code
)
{
	*powersources = 0;
	*powersourcesCnt = 0;
	*return_code = 0;

	@autoreleasepool
	{
		// Only internal batteries are known, so every pstype gets the same list
		NSData* data = [NSPropertyListSerialization dataWithPropertyList: internalBatteries()
			format: NSPropertyListBinaryFormat_v1_0 options: 0 error: nil];
		if (!data)
			return KERN_FAILURE;

		// The reply deallocates this buffer once it is sent
		kern_return_t kr = vm_allocate(mach_task_self(), powersources, [data length], VM_FLAGS_ANYWHERE);
		if (kr != KERN_SUCCESS)
		{
			*powersources = 0;
			return kr;
		}

		memcpy((void*) *powersources, [data bytes], [data length]);
		*powersourcesCnt = [data length];
		return KERN_SUCCESS;
	}
}
