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

/*
 * The identity of a host input device that is a USB device, as IOHIDDevice
 * publishes it: the manufacturer, product and serial number strings of the
 * USB device, its release number (bcdDevice) and its location. Linux
 * publishes these in sysfs, readable by anyone, on the USB device above the
 * evdev node. A device that is not a USB device (Bluetooth, virtual) has
 * none of this, and its properties stay what evdev reports for it.
 */

#ifndef _HOST_INPUT_IDENTITY_H
#define _HOST_INPUT_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iokitd_usb_identity
{
	int found;                 /* the node is behind a USB device */
	char manufacturer[128];    /* empty if the device has no such string */
	char product[128];
	char serial[128];
	int has_version;
	uint16_t version;          /* bcdDevice */
	int has_location;
	uint32_t location;         /* see iokitd_usb_location() */
};

/*
 * The identity of the USB device behind an evdev node. eventPath is
 * /dev/input/eventN. Reads the host's sysfs where the caller sees it.
 * Returns 1 if the node is behind a USB device (*out is filled), else 0.
 */
int iokitd_input_usb_identity(const char* eventPath, struct iokitd_usb_identity* out);

/* The same with the directory sysfs is mounted at (for testing) and the name of the node (eventN) */
int iokitd_input_usb_identity_at(const char* sysroot, const char* eventName, struct iokitd_usb_identity* out);

/*
 * The LocationID of a USB device the way macOS forms it: the number of the bus
 * in the top byte, then four bits for the port of every tier of hubs from the
 * root, the first tier highest. devpath is the port chain Linux names the
 * device by ("2" for a device on port 2 of the root hub, "4.1.3" behind two
 * hubs). A port above 15 does not fit its nibble and reads 15, tiers beyond
 * the sixth are left out. Returns 0 for a devpath that is not a chain of port numbers.
 */
uint32_t iokitd_usb_location(unsigned bus, const char* devpath);

/* The Manufacturer string; empty if the node is not behind a USB device or the device has none. */
void iokitd_input_manufacturer(const char* eventPath, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
