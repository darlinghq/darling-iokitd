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
 * The manufacturer string of a host input device (the IOHIDDevice
 * property "Manufacturer"). A USB device carries it in its string
 * descriptor, which Linux publishes in sysfs. A device the kernel has no
 * manufacturer for (Bluetooth, virtual) has none.
 */

#ifndef _HOST_INPUT_IDENTITY_H
#define _HOST_INPUT_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* eventPath is /dev/input/eventN. Empty result if the manufacturer is not known. */
void iokitd_input_manufacturer(const char* eventPath, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
