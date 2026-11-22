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

#include "IOHIDHostIdentity.h"
#include "HostInputIdentity.h"
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>

void IOHIDApplyHostIdentity(NSMutableDictionary* props, const char* eventPath)
{
	struct iokitd_usb_identity usb;

	props[@"CountryCode"] = @0;
	if (!iokitd_input_usb_identity(eventPath, &usb))
		return;

	if (usb.manufacturer[0])
		props[@"Manufacturer"] = [NSString stringWithUTF8String: usb.manufacturer];
	if (usb.product[0])
		props[@"Product"] = [NSString stringWithUTF8String: usb.product];
	if (usb.serial[0])
		props[@"SerialNumber"] = [NSString stringWithUTF8String: usb.serial];
	if (usb.has_version)
		props[@"VersionNumber"] = @(usb.version);
	if (usb.has_location)
		props[@"LocationID"] = @(usb.location);
}
