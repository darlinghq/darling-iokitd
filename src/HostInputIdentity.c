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

#include "HostInputIdentity.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Where the guest sees the host's sysfs, and where a host sees its own */
static const char* const kSysfsRoots[] = { "/Volumes/SystemRoot/sys", "/sys" };

/* A one-line sysfs attribute without its newline; 0 if it is missing or empty */
static int read_attribute(const char* dir, const char* name, char* out, size_t size)
{
	char file[PATH_MAX + 32];
	FILE* f;

	snprintf(file, sizeof(file), "%s/%s", dir, name);
	f = fopen(file, "r");
	if (!f)
		return 0;
	if (!fgets(out, (int) size, f))
		out[0] = 0;
	fclose(f);
	out[strcspn(out, "\n")] = 0;
	return out[0] != 0;
}

uint32_t iokitd_usb_location(unsigned bus, const char* devpath)
{
	uint32_t location = (uint32_t) (bus & 0xff) << 24;
	int tier = 0;

	if (!devpath || !devpath[0])
		return 0;
	for (const char* c = devpath; *c; )
	{
		char* end;
		unsigned long port = strtoul(c, &end, 10);

		if (end == c || (*end != '.' && *end != '\0'))
			return 0;
		if (tier < 6)
			location |= (uint32_t) (port > 15 ? 15 : port) << (20 - 4 * tier);
		tier++;
		c = *end ? end + 1 : end;
	}
	return location;
}

int iokitd_input_usb_identity_at(const char* sysroot, const char* eventName, struct iokitd_usb_identity* out)
{
	char link[PATH_MAX], dir[PATH_MAX], text[128];

	memset(out, 0, sizeof(*out));
	if (strncmp(eventName, "event", 5) != 0)
		return 0;
	snprintf(link, sizeof(link), "%s/class/input/%s/device", sysroot, eventName);
	if (!realpath(link, dir))
		return 0;

	/* The USB device is the ancestor directory that has idVendor */
	for (int depth = 0; depth < 12; depth++)
	{
		char file[PATH_MAX + 16];

		snprintf(file, sizeof(file), "%s/idVendor", dir);
		if (access(file, R_OK) == 0)
		{
			unsigned bus = 0;

			out->found = 1;
			read_attribute(dir, "manufacturer", out->manufacturer, sizeof(out->manufacturer));
			read_attribute(dir, "product", out->product, sizeof(out->product));
			read_attribute(dir, "serial", out->serial, sizeof(out->serial));
			if (read_attribute(dir, "bcdDevice", text, sizeof(text)))
			{
				char* end;
				unsigned long v = strtoul(text, &end, 16);

				if (end != text && *end == '\0' && v <= 0xffff)
				{
					out->has_version = 1;
					out->version = (uint16_t) v;
				}
			}
			if (read_attribute(dir, "busnum", text, sizeof(text)))
				bus = (unsigned) strtoul(text, NULL, 10);
			if (bus && read_attribute(dir, "devpath", text, sizeof(text)))
			{
				out->location = iokitd_usb_location(bus, text);
				out->has_location = out->location != 0;
			}
			return 1;
		}
		char* slash = strrchr(dir, '/');
		if (!slash || slash == dir)
			break;
		*slash = 0;
	}
	return 0;
}

int iokitd_input_usb_identity(const char* eventPath, struct iokitd_usb_identity* out)
{
	const char* base = strrchr(eventPath, '/');

	memset(out, 0, sizeof(*out));
	base = base ? base + 1 : eventPath;
	for (size_t i = 0; i < sizeof(kSysfsRoots) / sizeof(kSysfsRoots[0]); i++)
		if (iokitd_input_usb_identity_at(kSysfsRoots[i], base, out))
			return 1;
	return 0;
}

void iokitd_input_manufacturer(const char* eventPath, char* out, size_t size)
{
	struct iokitd_usb_identity identity;

	out[0] = 0;
	if (iokitd_input_usb_identity(eventPath, &identity))
		snprintf(out, size, "%s", identity.manufacturer);
}
