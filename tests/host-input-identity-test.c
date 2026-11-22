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
 * Test driver for HostInputIdentity.c, built natively on Linux by
 * tests/host-input-identity.py.
 *
 *   host-input-identity-test location <bus> <devpath>
 *       prints the LocationID in hex;
 *   host-input-identity-test identity <sysfs root> <event name>
 *       prints what the USB device behind the node has, or "none".
 */
#include "../src/HostInputIdentity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
	if (argc == 4 && strcmp(argv[1], "location") == 0)
	{
		printf("%08x\n", iokitd_usb_location((unsigned) atoi(argv[2]), argv[3]));
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "identity") == 0)
	{
		struct iokitd_usb_identity id;

		if (!iokitd_input_usb_identity_at(argv[2], argv[3], &id))
		{
			printf("none\n");
			return 0;
		}
		printf("manufacturer=[%s] product=[%s] serial=[%s] version=%s%04x location=%s%08x\n", id.manufacturer, id.product, id.serial,
			id.has_version ? "" : "none:", id.version, id.has_location ? "" : "none:", id.location);
		return 0;
	}
	return 2;
}
