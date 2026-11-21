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

#ifndef IOKITD_IOHIDDEVICEMATCHING_H
#define IOKITD_IOHIDDEVICEMATCHING_H

#import <Foundation/NSDictionary.h>

// Matching of IOHIDDevice services. IOHIDManager matches devices by usage
// page and usage with the keys DeviceUsagePage and DeviceUsage, which are
// not properties of the device: they match any of the pairs in the device's
// DeviceUsagePairs property (a key alone matches on that part of the pair).
//
// Returns whether those keys of matching accept the device described by
// properties, and puts the rest of matching, to be compared with the device's
// properties as usual, in *remainder.
bool IOHIDMatchUsagePairs(NSDictionary* matching, NSDictionary* properties, NSDictionary** remainder);

#endif
