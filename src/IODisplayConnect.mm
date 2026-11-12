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

#include "IODisplayConnect.h"
#import <Foundation/NSString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSNumber.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include "HostDisplay.h"
#include <cstring>

const char* IODisplayConnect::className() const
{
	return "IODisplayConnect";
}

NSDictionary* IODisplayConnect::matchingDictionary()
{
	const char* clsName = typeid(*this).name();
	return @{
		@"IOProviderClass": @"IODisplayConnect",
		@"IOClass": [NSString stringWithUTF8String: clsName]
	};
}

bool IODisplayConnect::conformsTo(const char* className)
{
	if (std::strcmp(className, "IODisplay") == 0)
		return true;
	return IOService::conformsTo(className);
}

NSDictionary* IODisplayConnect::makeProperties(NSString* outputName, NSData* edid, int displayIndex, bool useEDIDName)
{
	NSMutableDictionary* props = [NSMutableDictionary dictionaryWithCapacity: 10];
	NSString* name = outputName;
	host_edid_info info;

	if (edid && host_edid_parse((const uint8_t*) [edid bytes], [edid length], &info) == 0)
	{
		if (useEDIDName && info.productName[0])
			name = [NSString stringWithUTF8String: info.productName];

		props[@(kIODisplayEDIDKey)] = edid;
		props[@(kDisplaySerialNumber)] = @(info.serialNumber);
		props[@(kDisplayProductID)] = @(info.productID);
		props[@(kDisplayVendorID)] = @(info.vendorID);
		props[@(kDisplayWeekOfManufacture)] = @(info.week);
		props[@(kDisplayYearOfManufacture)] = @(info.year);
		if (info.serialString[0])
			props[@(kDisplaySerialString)] = [NSString stringWithUTF8String: info.serialString];
		if (info.widthMM != 0 && info.heightMM != 0)
		{
			props[@(kDisplayHorizontalImageSize)] = @(info.widthMM);
			props[@(kDisplayVerticalImageSize)] = @(info.heightMM);
		}
	}
	else
	{
		uint8_t fakeEDID[HOST_EDID_BLOCK_SIZE];

		memset(fakeEDID, 0, sizeof(fakeEDID));
		memset(fakeEDID + 1, 0xff, 6);
		fakeEDID[12] = displayIndex + 1;

		uint8_t csum = 0;
		for (size_t i = 0; i < sizeof(fakeEDID); i++)
			csum += fakeEDID[i];
		fakeEDID[127] = (256 - csum) % 256;

		props[@(kIODisplayEDIDKey)] = [NSData dataWithBytes: fakeEDID length: sizeof(fakeEDID)];
		props[@(kDisplaySerialNumber)] = @(displayIndex + 1);
	}

	props[@(kDisplayProductName)] = @{ @"en_US": name };
	return props;
}
