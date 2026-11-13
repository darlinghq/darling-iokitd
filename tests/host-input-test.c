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
 * Test driver for HostInput.c, built natively on Linux by
 * tests/host-input.py.
 *
 *   host-input-test layout
 *       checks the report descriptors and the event folding for a described
 *       keyboard and mouse, without any device;
 *   host-input-test probe <event node>
 *       prints "keyboard <name> <vendor> <product> <report size>" or
 *       "mouse ...", or "none" when the node is neither;
 *   host-input-test watch <event node> <count>
 *       prints the hex input report after the initial state and then after
 *       every frame that changes it, until <count> changes were seen (30 s
 *       limit).
 */
#include "../src/HostInput.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

#define EV_KEY 1
#define EV_REL 2

static int layout(void)
{
	uint8_t descriptor[256];

	/* keyboard */
	struct iokitd_input kbd;
	memset(&kbd, 0, sizeof(kbd));
	kbd.kind = IOKITD_INPUT_KEYBOARD;
	kbd.report_size = IOKITD_KEYBOARD_REPORT_SIZE;

	static const uint8_t kbd_expected[] = {
		0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
		0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
		0x19, 0x04, 0x29, 0x73, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x70, 0x81, 0x02,
		0xc0,
	};
	int length = iokitd_input_report_descriptor(&kbd, descriptor, sizeof(descriptor));
	CHECK(length == (int) sizeof(kbd_expected));
	CHECK(length == (int) sizeof(kbd_expected) && memcmp(descriptor, kbd_expected, sizeof(kbd_expected)) == 0);
	CHECK(iokitd_input_report_descriptor(&kbd, descriptor, 10) == -1);
	CHECK(iokitd_input_primary_usage(&kbd) == 6);

	/* the usage of a key follows its position, not its evdev code */
	CHECK(iokitd_input_keyboard_usage(30) == 0x04);   /* A */
	CHECK(iokitd_input_keyboard_usage(44) == 0x1d);   /* Z */
	CHECK(iokitd_input_keyboard_usage(2) == 0x1e);    /* 1 */
	CHECK(iokitd_input_keyboard_usage(11) == 0x27);   /* 0 */
	CHECK(iokitd_input_keyboard_usage(28) == 0x28);   /* Enter */
	CHECK(iokitd_input_keyboard_usage(57) == 0x2c);   /* Space */
	CHECK(iokitd_input_keyboard_usage(59) == 0x3a);   /* F1 */
	CHECK(iokitd_input_keyboard_usage(88) == 0x45);   /* F12 */
	CHECK(iokitd_input_keyboard_usage(183) == 0x68);  /* F13 */
	CHECK(iokitd_input_keyboard_usage(194) == 0x73);  /* F24 */
	CHECK(iokitd_input_keyboard_usage(103) == 0x52);  /* Up */
	CHECK(iokitd_input_keyboard_usage(125) == 0xe3);  /* Left Meta (Command) */
	CHECK(iokitd_input_keyboard_usage(0) == 0);
	CHECK(iokitd_input_keyboard_usage(0x110) == 0);   /* a mouse button */

	uint8_t report[IOKITD_KEYBOARD_REPORT_SIZE] = {0};

	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 30, 1) == 1);       /* A down */
	CHECK(report[0] == 0 && report[1] == 0x01);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 30, 2) == 0);       /* auto-repeat */
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 30, 1) == 0);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 42, 1) == 1);       /* Left Shift */
	CHECK(report[0] == 0x02);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 97, 1) == 1);       /* Right Control */
	CHECK(report[0] == 0x12);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 194, 1) == 1);      /* F24: last bit */
	CHECK(report[14] == 0x80);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 30, 0) == 1);
	CHECK(report[1] == 0);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 0x110, 1) == 0);    /* not a keyboard key */
	CHECK(iokitd_input_apply_event(&kbd, report, EV_REL, 0, 5) == 0);
	CHECK(iokitd_input_apply_event(&kbd, report, EV_KEY, 99, 1) == 1);       /* Print Screen */
	CHECK(report[1 + (0x46 - 4) / 8] == (1 << ((0x46 - 4) % 8)));

	/* mouse with 3 buttons, wheel and horizontal wheel */
	struct iokitd_input mouse;
	memset(&mouse, 0, sizeof(mouse));
	mouse.kind = IOKITD_INPUT_MOUSE;
	mouse.button_count = 3;
	mouse.has_wheel = mouse.has_hwheel = 1;
	mouse.x_offset = 1; mouse.y_offset = 3; mouse.wheel_offset = 5; mouse.hwheel_offset = 6;
	mouse.report_size = 7;

	static const uint8_t mouse_expected[] = {
		0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
		0x75, 0x01, 0x95, 0x05, 0x81, 0x03,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06,
		0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
		0x05, 0x0c, 0x0a, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
		0xc0, 0xc0,
	};
	length = iokitd_input_report_descriptor(&mouse, descriptor, sizeof(descriptor));
	CHECK(length == (int) sizeof(mouse_expected));
	CHECK(length == (int) sizeof(mouse_expected) && memcmp(descriptor, mouse_expected, sizeof(mouse_expected)) == 0);
	CHECK(iokitd_input_primary_usage(&mouse) == 2);

	/* five buttons and no wheels: three padding bits, no wheel items */
	struct iokitd_input plain = mouse;
	plain.button_count = 5;
	plain.has_wheel = plain.has_hwheel = 0;
	plain.report_size = 5;
	length = iokitd_input_report_descriptor(&plain, descriptor, sizeof(descriptor));
	CHECK(length == 52);

	uint8_t m[7] = {0};

	CHECK(iokitd_input_apply_event(&mouse, m, EV_KEY, 0x110, 1) == 1);       /* left */
	CHECK(m[0] == 1);
	CHECK(iokitd_input_apply_event(&mouse, m, EV_KEY, 0x112, 1) == 1);       /* middle */
	CHECK(m[0] == 5);
	CHECK(iokitd_input_apply_event(&mouse, m, EV_KEY, 0x113, 1) == 0);       /* side: beyond button_count */
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 0, 10) == 1);          /* X */
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 0, -3) == 1);
	CHECK(m[1] == 7 && m[2] == 0);                                           /* motion accumulates */
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 1, -300) == 1);        /* Y, down is positive */
	CHECK(m[3] == 0xd4 && m[4] == 0xfe);                                     /* -300 = 0xfed4 */
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 0, 100000) == 1);      /* clamped */
	CHECK(m[1] == 0xff && m[2] == 0x7f);
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 8, 1) == 1);           /* wheel */
	CHECK(m[5] == 1);
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 8, -500) == 1);
	CHECK(m[5] == 0x81);                                                     /* -127 */
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 6, 2) == 1);           /* horizontal wheel */
	CHECK(m[6] == 2);
	CHECK(iokitd_input_apply_event(&mouse, m, EV_REL, 0x0b, 5) == 0);        /* hi-res wheel is ignored */

	plain.report_size = 5;
	memset(m, 0, sizeof(m));
	plain.x_offset = 1; plain.y_offset = 3;
	CHECK(iokitd_input_apply_event(&plain, m, EV_REL, 8, 1) == 0);           /* no wheel declared */

	printf(failures ? "layout: FAILED\n" : "layout: ok\n");
	return failures != 0;
}

static int changes_seen;

static void print_report(const struct iokitd_input* dev, const uint8_t* report)
{
	printf("report");
	for (int i = 0; i < dev->report_size; i++)
		printf(" %02x", report[i]);
	printf("\n");
	fflush(stdout);
}

static void print_frame(const uint8_t* report, void* context)
{
	print_report(context, report);
	changes_seen++;
}

int main(int argc, char** argv)
{
	if (argc == 2 && !strcmp(argv[1], "layout"))
		return layout();

	if (argc >= 3 && !strcmp(argv[1], "probe"))
	{
		struct iokitd_input dev;
		if (iokitd_input_probe(argv[2], &dev) != 0)
		{
			printf("none\n");
			return 0;
		}
		printf("%s %s|%04x|%04x|%d\n", dev.kind == IOKITD_INPUT_KEYBOARD ? "keyboard" : "mouse",
			dev.name, dev.vendor, dev.product, dev.report_size);
		return 0;
	}

	if (argc >= 4 && !strcmp(argv[1], "watch"))
	{
		struct iokitd_input dev;
		uint8_t report[IOKITD_INPUT_MAX_REPORT];
		int changes = atoi(argv[3]);

		if (iokitd_input_probe(argv[2], &dev) != 0)
			return 2;
		int fd = iokitd_input_open(argv[2]);
		if (fd < 0)
			return 2;
		iokitd_input_initial_report(fd, &dev, report);
		print_report(&dev, report);

		while (changes_seen < changes)
		{
			struct pollfd p = { fd, POLLIN, 0 };
			if (poll(&p, 1, 30000) <= 0)
				return 3;
			if (iokitd_input_read(fd, &dev, report, print_frame, &dev) < 0)
			{
				printf("gone\n");
				return 0;
			}
		}
		return 0;
	}

	fprintf(stderr, "usage: see the file header\n");
	return 1;
}
