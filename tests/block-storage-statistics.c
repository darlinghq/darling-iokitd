/*
 * This file is part of Darling.
 *
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Darling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Darling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Darling.  If not, see <http://www.gnu.org/licenses/>.
 */

// IOServiceMatching("IOBlockStorageDriver") finds the disks of the host, and the
// "Statistics" property of each is a dictionary with the number of read and write
// operations and the bytes read and written since boot, which only ever grow.

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL line %d: %s\n", __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static const char* const keys[4] = { "Operations (Read)", "Bytes (Read)", "Operations (Write)", "Bytes (Write)" };

// the sums of the four counters over all drives; returns the number of drives
static int sample(uint64_t sums[4])
{
	io_iterator_t iterator = 0;
	int drives = 0;

	for (int i = 0; i < 4; i++)
		sums[i] = 0;

	CHECK(IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOBlockStorageDriver"), &iterator) == KERN_SUCCESS);
	io_registry_entry_t drive;
	while ((drive = IOIteratorNext(iterator)) != 0) {
		CFMutableDictionaryRef properties = NULL;
		CHECK(IORegistryEntryCreateCFProperties(drive, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS);
		CFDictionaryRef statistics = properties ? CFDictionaryGetValue(properties, CFSTR("Statistics")) : NULL;
		CHECK(statistics && CFGetTypeID(statistics) == CFDictionaryGetTypeID());
		for (int i = 0; statistics && i < 4; i++) {
			CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
			CFNumberRef number = CFDictionaryGetValue(statistics, key);
			SInt64 value = -1;
			CHECK(number && CFGetTypeID(number) == CFNumberGetTypeID());
			if (number)
				CHECK(CFNumberGetValue(number, kCFNumberSInt64Type, &value) && value >= 0);
			if (value > 0)
				sums[i] += value;
			CFRelease(key);
		}
		if (properties)
			CFRelease(properties);
		IOObjectRelease(drive);
		drives++;
	}
	IOObjectRelease(iterator);
	return drives;
}

int main(void)
{
	uint64_t first[4], second[4];
	int drives = sample(first);
	// a host has a disk (a build host with only virtual disks is not one this test can run on)
	CHECK(drives >= 1);

	// the bytes are whole sectors of the disk, the operations come with them
	CHECK((first[1] == 0) == (first[0] == 0));
	CHECK((first[3] == 0) == (first[2] == 0));
	CHECK(first[1] % 512 == 0 && first[3] % 512 == 0);

	// the counters never go back
	sample(second);
	for (int i = 0; i < 4; i++)
		CHECK(second[i] >= first[i]);

	if (failures)
		return 1;
	puts("PASS");
	return 0;
}
