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

#ifndef IOKITD_IOHIDHOSTIDENTITY_H
#define IOKITD_IOHIDHOSTIDENTITY_H

#import <Foundation/NSDictionary.h>

// Completes the properties of an IOHIDDevice made for a host input node with
// what the host knows of the device. CountryCode is 0 (no localization) for
// all. For a USB device the strings of the USB device (Manufacturer, Product,
// SerialNumber), its release number (VersionNumber) and its location
// (LocationID, as macOS forms it from the bus and the hub ports) replace what
// evdev reports; whatever the USB device has no value for stays as it is.
void IOHIDApplyHostIdentity(NSMutableDictionary* properties, const char* eventPath);

#endif
