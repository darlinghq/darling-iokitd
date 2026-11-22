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

/* The USB device above an input node: the ancestor directory that has both
   idVendor and manufacturer */
static int sysfs_usb_manufacturer(const char* eventPath, char* out, size_t size)
{
	const char* base = strrchr(eventPath, '/');
	char link[PATH_MAX], dir[PATH_MAX];

	if (!base || strncmp(base + 1, "event", 5) != 0)
		return 0;
	snprintf(link, sizeof(link), "/sys/class/input/%s/device", base + 1);
	if (!realpath(link, dir))
		return 0;

	for (int depth = 0; depth < 12; depth++)
	{
		char file[PATH_MAX + 16];
		FILE* f;

		snprintf(file, sizeof(file), "%s/idVendor", dir);
		f = fopen(file, "r");
		if (f)
		{
			fclose(f);
			snprintf(file, sizeof(file), "%s/manufacturer", dir);
			f = fopen(file, "r");
			if (!f)
				return 0;
			if (!fgets(out, (int) size, f))
				out[0] = 0;
			fclose(f);
			out[strcspn(out, "\n")] = 0;
			return out[0] != 0;
		}
		char* slash = strrchr(dir, '/');
		if (!slash || slash == dir)
			break;
		*slash = 0;
	}
	return 0;
}

void iokitd_input_manufacturer(const char* eventPath, char* out, size_t size)
{
	out[0] = 0;
	sysfs_usb_manufacturer(eventPath, out, size);
}
