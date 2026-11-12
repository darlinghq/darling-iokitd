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

// Display identification that does not depend on a display server: the
// base EDID block (VESA E-EDID) and the connectors the kernel's DRM
// subsystem lists in sysfs. Plain C so that it can be tested on the host.

#ifndef IOKITD_HOSTDISPLAY_H
#define IOKITD_HOSTDISPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_EDID_BLOCK_SIZE 128

typedef struct
{
	// The three letters of the manufacturer's PNP ID, NUL-terminated
	char manufacturer[4];
	// The same ID as the 16-bit value stored in the EDID (big-endian bytes 8 and 9)
	uint16_t vendorID;
	uint16_t productID;
	uint32_t serialNumber;
	// 0 if the EDID does not give a week of manufacture
	int week;
	int year;
	// Display product name and serial number descriptors; empty if absent
	char productName[14];
	char serialString[14];
	// Physical image size in millimetres; 0 if unknown (e.g. a projector)
	unsigned widthMM;
	unsigned heightMM;
	int digital;
} host_edid_info;

// Parses the first 128-byte block of an EDID. Returns 0 if the block is
// there, has the fixed header and a valid checksum, and -1 otherwise.
int host_edid_parse(const uint8_t* edid, size_t length, host_edid_info* info);

typedef struct
{
	// The connector's name without the card prefix, e.g. "eDP-1"
	char name[64];
	int enabled;
	// The raw EDID, or NULL/0 if the connector has none (malloc'd)
	uint8_t* edid;
	size_t edidLength;
} host_drm_connector;

// Lists the connected, enabled connectors found in a DRM sysfs class
// directory (normally /sys/class/drm, "cardN-<connector>" entries). Fills
// at most max entries of out and returns how many were found; free each
// with host_drm_connector_free. Returns 0 if the directory is not readable.
size_t host_drm_connectors(const char* sysfsDirectory, host_drm_connector* out, size_t max);
void host_drm_connector_free(host_drm_connector* connector);

#ifdef __cplusplus
}
#endif

#endif
