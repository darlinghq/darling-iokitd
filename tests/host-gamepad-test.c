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
 * Test driver for HostGamepad.c, built natively on Linux by
 * tests/host-gamepad.py.
 *
 *   host-gamepad-test layout
 *       checks the report descriptor and the event folding for a described
 *       controller, without any device;
 *   host-gamepad-test probe <event node>
 *       prints "pad <name> <vendor> <product> <buttons> <axes> <hat> <size>",
 *       or "none" when the node is not a game controller;
 *   host-gamepad-test watch <event node> <count>
 *       prints the hex input report after the initial state and then after
 *       every change, until <count> changes were seen (30 s limit).
 */
#include "../src/HostGamepad.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static int layout(void)
{
	struct iokitd_gamepad pad;
	memset(&pad, 0, sizeof(pad));
	pad.has_gamepad_buttons = 1;
	pad.button_count = 2;
	pad.buttons[0].code = 0x130; pad.buttons[0].usage = 1;
	pad.buttons[1].code = 0x131; pad.buttons[1].usage = 2;
	pad.has_hat = 1;
	pad.hat_x_min = pad.hat_y_min = -1;
	pad.hat_x_max = pad.hat_y_max = 1;
	pad.hat_byte_offset = 1;
	pad.axis_count = 3;
	pad.axes[0] = (struct iokitd_gamepad_axis){ 0, 1, 0x30, -32768, 32767, 16, 2 };
	pad.axes[1] = (struct iokitd_gamepad_axis){ 1, 1, 0x31, -32768, 32767, 16, 4 };
	pad.axes[2] = (struct iokitd_gamepad_axis){ 2, 1, 0x32, 0, 255, 16, 6 };
	pad.report_size = 8;

	static const uint8_t expected[] = {
		0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
		0x05, 0x09, 0x09, 0x01, 0x09, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
		0x75, 0x01, 0x95, 0x06, 0x81, 0x03,
		0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
		0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
		0x05, 0x01, 0x09, 0x30, 0x16, 0x00, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x01, 0x09, 0x31, 0x16, 0x00, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x01, 0x09, 0x32, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02,
		0xc0,
	};
	uint8_t descriptor[256];
	int length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));

	CHECK(length == (int) sizeof(expected));
	CHECK(length == (int) sizeof(expected) && memcmp(descriptor, expected, sizeof(expected)) == 0);
	CHECK(iokitd_gamepad_report_descriptor(&pad, descriptor, 10) == -1);
	CHECK(iokitd_gamepad_primary_usage(&pad) == 5);

	uint8_t report[8] = {0};

	/* buttons */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 1) == 1);
	CHECK(report[0] == 0x02);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 1) == 0);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x130, 1) == 1);
	CHECK(report[0] == 0x03);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 0) == 1);
	CHECK(report[0] == 0x01);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x13c, 1) == 0); /* not a button of this pad */

	/* hat: the two axes arrive separately */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, -1) == 1);
	CHECK(report[1] == 1);                                              /* up */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 1) == 1);
	CHECK(report[1] == 2);                                              /* up-right */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, 0) == 1);
	CHECK(report[1] == 3);                                              /* right */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, -1) == 1);
	CHECK(report[1] == 7);                                              /* left */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 0) == 1);
	CHECK(report[1] == 0);                                              /* centred: null state */

	/* axes: little endian, clamped */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0, -2) == 1);
	CHECK(report[2] == 0xfe && report[3] == 0xff);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 1, 100000) == 1);
	CHECK(report[4] == 0xff && report[5] == 0x7f);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 2, 200) == 1);
	CHECK(report[6] == 200 && report[7] == 0);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 2, -5) == 1);
	CHECK(report[6] == 0 && report[7] == 0);

	/* a joystick without the gamepad block */
	pad.has_gamepad_buttons = 0;
	CHECK(iokitd_gamepad_primary_usage(&pad) == 4);

	printf(failures ? "layout: FAILED\n" : "layout: ok\n");
	return failures != 0;
}

static void print_report(const struct iokitd_gamepad* pad, const uint8_t* report)
{
	printf("report");
	for (int i = 0; i < pad->report_size; i++)
		printf(" %02x", report[i]);
	printf("\n");
	fflush(stdout);
}

int main(int argc, char** argv)
{
	if (argc == 2 && !strcmp(argv[1], "layout"))
		return layout();

	if (argc >= 3 && !strcmp(argv[1], "probe"))
	{
		struct iokitd_gamepad pad;
		if (iokitd_gamepad_probe(argv[2], &pad) != 0)
		{
			printf("none\n");
			return 0;
		}
		printf("pad %s|%04x|%04x|%d|%d|%d|%d\n", pad.name, pad.vendor, pad.product,
			pad.button_count, pad.axis_count, pad.has_hat, pad.report_size);
		return 0;
	}

	if (argc >= 4 && !strcmp(argv[1], "watch"))
	{
		struct iokitd_gamepad pad;
		uint8_t report[128];
		int changes = atoi(argv[3]);

		if (iokitd_gamepad_probe(argv[2], &pad) != 0)
			return 2;
		int fd = iokitd_gamepad_open(argv[2]);
		if (fd < 0)
			return 2;
		iokitd_gamepad_initial_report(fd, &pad, report);
		print_report(&pad, report);

		while (changes > 0)
		{
			struct pollfd p = { fd, POLLIN, 0 };
			if (poll(&p, 1, 30000) <= 0)
				return 3;
			int r = iokitd_gamepad_read(fd, &pad, report);
			if (r < 0)
			{
				printf("gone\n");
				return 0;
			}
			if (r == 1)
			{
				print_report(&pad, report);
				changes--;
			}
		}
		return 0;
	}

	fprintf(stderr, "usage: see the file header\n");
	return 1;
}
