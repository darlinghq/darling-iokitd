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

static void fill_standard(struct iokitd_gamepad* pad)
{
	memset(pad, 0, sizeof(*pad));
	pad->has_gamepad_buttons = 1;
	pad->layout = IOKITD_GAMEPAD_LAYOUT_STANDARD;
	pad->button_count = 2;
	pad->buttons[0].code = 0x130; pad->buttons[0].usage = 1;
	pad->buttons[1].code = 0x131; pad->buttons[1].usage = 2;
	pad->button_bits = 15;
	pad->has_hat = 1;
	pad->hat_x_min = pad->hat_y_min = -1;
	pad->hat_x_max = pad->hat_y_max = 1;
	pad->hat_logical_min = 1;
	pad->axis_count = 2;
	pad->axes[0] = (struct iokitd_gamepad_axis){ 0, 1, 0x30, -32768, 32767, 16 };
	pad->axes[1] = (struct iokitd_gamepad_axis){ 2, 2, 0xc5, 0, 1023, 10 };
	iokitd_gamepad_layout(pad);
}

static void fill_playstation(struct iokitd_gamepad* pad)
{
	memset(pad, 0, sizeof(*pad));
	pad->has_gamepad_buttons = 1;
	pad->layout = IOKITD_GAMEPAD_LAYOUT_PLAYSTATION;
	pad->button_count = 2;
	pad->buttons[0].code = 0x130; pad->buttons[0].usage = 2;
	pad->buttons[1].code = 0x134; pad->buttons[1].usage = 1;
	pad->button_bits = 14;
	pad->has_hat = 1;
	pad->hat_x_min = pad->hat_y_min = -1;
	pad->hat_x_max = pad->hat_y_max = 1;
	pad->hat_logical_min = 0;
	pad->axis_count = 2;
	/* in rank order: the left stick X, then the left trigger (Rx), which the report puts after the buttons */
	pad->axes[0] = (struct iokitd_gamepad_axis){ 0, 1, 0x30, 0, 255, 8 };
	pad->axes[1] = (struct iokitd_gamepad_axis){ 2, 1, 0x33, 0, 255, 8 };
	iokitd_gamepad_layout(pad);
}

static int layout(void)
{
	struct iokitd_gamepad pad;
	uint8_t descriptor[256];

	/* Standard layout: axes, the hat (1 .. 8, null state 0), 15 contiguous button usages */
	fill_standard(&pad);
	static const uint8_t standard[] = {
		0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x02, 0x0a, 0xc5, 0x00, 0x15, 0x00, 0x26, 0xff, 0x03, 0x75, 0x0a, 0x95, 0x01, 0x81, 0x02,
		0x75, 0x01, 0x95, 0x06, 0x81, 0x03,
		0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
		0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00,
		0x75, 0x01, 0x95, 0x04, 0x81, 0x03,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x0f, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0f, 0x81, 0x02,
		0x75, 0x01, 0x95, 0x01, 0x81, 0x03,
		0xc0,
	};
	int length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));

	CHECK(length == (int) sizeof(standard));
	CHECK(length == (int) sizeof(standard) && memcmp(descriptor, standard, sizeof(standard)) == 0);
	CHECK(iokitd_gamepad_report_descriptor(&pad, descriptor, 10) == -1);
	CHECK(iokitd_gamepad_primary_usage(&pad) == 5);
	CHECK(pad.report_size == 7);
	CHECK(pad.fields[0].bit_offset == 0 && pad.fields[1].bit_offset == 16 && pad.fields[2].bit_offset == 32
		&& pad.fields[3].bit_offset == 40);

	uint8_t report[8] = {0};

	/* buttons: usage n is bit n - 1 of the 15 button bits, which start at bit 40 */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 1) == 1);
	CHECK(report[5] == 0x02);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 1) == 0);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x130, 1) == 1);
	CHECK(report[5] == 0x03);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x131, 0) == 1);
	CHECK(report[5] == 0x01);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x13c, 1) == 0); /* not a button of this pad */

	/* hat: the two axes arrive separately; centred is the null state 0, outside 1 .. 8 */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, -1) == 1);
	CHECK(report[4] == 1);                                              /* up */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 1) == 1);
	CHECK(report[4] == 2);                                              /* up-right */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, 0) == 1);
	CHECK(report[4] == 3);                                              /* right */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, -1) == 1);
	CHECK(report[4] == 7);                                              /* left */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 0) == 1);
	CHECK(report[4] == 0);                                              /* centred: null state */

	/* axes: the offset from the minimum, little endian, clamped */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0, -2) == 1);
	CHECK(report[0] == 0xfe && report[1] == 0x7f);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0, 100000) == 1);
	CHECK(report[0] == 0xff && report[1] == 0xff);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 2, 1000) == 1);  /* 10 bits */
	CHECK(report[2] == 0xe8 && report[3] == 0x03);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 2, -5) == 1);
	CHECK(report[2] == 0 && report[3] == 0);

	/* a joystick without the gamepad block */
	pad.has_gamepad_buttons = 0;
	CHECK(iokitd_gamepad_primary_usage(&pad) == 4);

	/* PlayStation layout: the hat is 0 .. 7 (null state 8) and shares its byte with the buttons;
	 * the left trigger (Rx) follows the buttons */
	fill_playstation(&pad);
	static const uint8_t playstation[] = {
		0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
		0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
		0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x0e, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0e, 0x81, 0x02,
		0x75, 0x01, 0x95, 0x06, 0x81, 0x03,
		0x05, 0x01, 0x09, 0x33, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
		0xc0,
	};
	length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));
	CHECK(length == (int) sizeof(playstation));
	CHECK(length == (int) sizeof(playstation) && memcmp(descriptor, playstation, sizeof(playstation)) == 0);
	CHECK(pad.report_size == 5);

	memset(report, 0, sizeof(report));
	report[1] = 0x08;                                                   /* the centred hat, as the initial report has it */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, -1) == 1);
	CHECK((report[1] & 0x0f) == 0);                                     /* up is 0 */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 1) == 1);
	CHECK((report[1] & 0x0f) == 1);                                     /* up-right */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x10, 0) == 1);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 0x11, 0) == 1);
	CHECK((report[1] & 0x0f) == 8);                                     /* centred */
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x130, 1) == 1);  /* Cross is usage 2: bit 13 */
	CHECK(report[1] == 0x28);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 1, 0x134, 1) == 1);  /* Square is usage 1: bit 12 */
	CHECK(report[1] == 0x38);
	CHECK(iokitd_gamepad_apply_event(&pad, report, 3, 2, 255) == 1);
	CHECK(report[4] == 0xff);

	/* A controller that can rumble: its input report has ID 1 and it has the output report of its layout */
	fill_standard(&pad);
	pad.has_rumble = 1;
	pad.input_report_id = 1;
	pad.output_kind = IOKITD_GAMEPAD_OUTPUT_XBOX;
	pad.output_report_id = 3;
	pad.output_size = 8;
	static const uint8_t xbox_output[] = {
		0x05, 0x0f, 0x09, 0x21, 0xa1, 0x02, 0x85, 0x03,
		0x0a, 0x97, 0x00, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x04, 0x91, 0x02,
		0x75, 0x01, 0x95, 0x04, 0x91, 0x03,
		0x09, 0x70, 0x25, 0x64, 0x75, 0x08, 0x95, 0x04, 0x91, 0x02,
		0x09, 0x50, 0x26, 0xff, 0x00, 0x66, 0x01, 0x10, 0x55, 0x0e, 0x95, 0x01, 0x91, 0x02,
		0x0a, 0xa7, 0x00, 0x65, 0x00, 0x55, 0x00, 0x91, 0x02,
		0x09, 0x7c, 0x91, 0x02,
		0xc0,
		0xc0,
	};
	length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));
	CHECK(length == (int) (sizeof(standard) + 2 + sizeof(xbox_output) - 1));
	CHECK(descriptor[6] == 0x85 && descriptor[7] == 0x01);
	CHECK(length > (int) sizeof(xbox_output) && memcmp(descriptor + length - sizeof(xbox_output), xbox_output, sizeof(xbox_output)) == 0);

	struct iokitd_rumble rumble;
	/* enable bit 0 is the weak motor, bit 1 the strong one; magnitudes 0 .. 100 at indices 3 and 2;
	 * duration 10 ms units times the loop count */
	static const uint8_t play[8] = { 0x03, 0, 0, 50, 100, 20, 5, 2 };
	CHECK(iokitd_gamepad_decode_output(&pad, play, 8, &rumble) == 1);
	CHECK(rumble.strong == 32767 && rumble.weak == 65535 && rumble.length_ms == 400 && rumble.delay_ms == 50);
	static const uint8_t strong_only[8] = { 0x02, 0, 0, 100, 100, 0, 0, 0 };
	CHECK(iokitd_gamepad_decode_output(&pad, strong_only, 8, &rumble) == 1);
	CHECK(rumble.strong == 65535 && rumble.weak == 0 && rumble.length_ms == 0);
	static const uint8_t none_enabled[8] = { 0x00, 0, 0, 100, 100, 0, 0, 0 };
	CHECK(iokitd_gamepad_decode_output(&pad, none_enabled, 8, &rumble) == 1);
	CHECK(rumble.strong == 0 && rumble.weak == 0);
	CHECK(iokitd_gamepad_decode_output(&pad, play, 9, &rumble) == -1);   /* longer than the report */

	/* DualShock 4: report 5 (31 bytes), 17 (77 bytes) on Bluetooth; flag bit 0 enables rumble */
	fill_playstation(&pad);
	pad.has_rumble = 1;
	pad.input_report_id = 1;
	pad.output_kind = IOKITD_GAMEPAD_OUTPUT_DUALSHOCK;
	pad.output_report_id = 5;
	pad.output_size = 31;
	static const uint8_t ds4_output[] = {
		0x06, 0x00, 0xff, 0x09, 0x22, 0x85, 0x05, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x1f, 0x91, 0x02,
		0xc0,
	};
	length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));
	CHECK(length > (int) sizeof(ds4_output) && memcmp(descriptor + length - sizeof(ds4_output), ds4_output, sizeof(ds4_output)) == 0);
	uint8_t usb[31] = { 0x01, 0, 0, 0x80, 0xff };                        /* right (weak) 0x80, left (strong) 0xff */
	CHECK(iokitd_gamepad_decode_output(&pad, usb, 31, &rumble) == 1);
	CHECK(rumble.weak == 0x8080 && rumble.strong == 0xffff);
	usb[0] = 0x02;                                                      /* only the light bar */
	CHECK(iokitd_gamepad_decode_output(&pad, usb, 31, &rumble) == 0);
	pad.output_report_id = 0x11;
	pad.output_size = 77;
	uint8_t bluetooth[77] = { 0xc0, 0, 0x01, 0, 0, 0x10, 0x20 };         /* two bytes before the shared part */
	CHECK(iokitd_gamepad_decode_output(&pad, bluetooth, 77, &rumble) == 1);
	CHECK(rumble.weak == 0x1010 && rumble.strong == 0x2020);
	CHECK(iokitd_gamepad_decode_output(&pad, bluetooth, 78, &rumble) == -1);
	fill_standard(&pad);                                                /* no rumble: no output report */
	CHECK(iokitd_gamepad_decode_output(&pad, play, 8, &rumble) == -1);

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
