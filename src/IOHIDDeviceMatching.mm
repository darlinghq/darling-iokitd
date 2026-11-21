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

#include "IOHIDDeviceMatching.h"
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

static NSString* const kKeyDeviceUsagePage = @"DeviceUsagePage";
static NSString* const kKeyDeviceUsage = @"DeviceUsage";
static NSString* const kKeyDeviceUsagePairs = @"DeviceUsagePairs";

bool IOHIDMatchUsagePairs(NSDictionary* matching, NSDictionary* properties, NSDictionary** remainder)
{
	id page = matching[kKeyDeviceUsagePage];
	id usage = matching[kKeyDeviceUsage];

	*remainder = matching;
	if (!page && !usage)
		return true;

	NSMutableDictionary* rest = [NSMutableDictionary dictionaryWithDictionary: matching];
	[rest removeObjectForKey: kKeyDeviceUsagePage];
	[rest removeObjectForKey: kKeyDeviceUsage];
	*remainder = rest;

	id pairs = properties[kKeyDeviceUsagePairs];
	if (![pairs isKindOfClass: [NSArray class]])
		return false;

	for (id pair in (NSArray*) pairs)
	{
		if (![pair isKindOfClass: [NSDictionary class]])
			continue;
		if ((!page || [pair[kKeyDeviceUsagePage] isEqual: page]) && (!usage || [pair[kKeyDeviceUsage] isEqual: usage]))
			return true;
	}
	return false;
}
