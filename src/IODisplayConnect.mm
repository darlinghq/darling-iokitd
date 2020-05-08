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
