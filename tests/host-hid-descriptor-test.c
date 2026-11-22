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
 * Test driver for HostHIDDescriptor.c and HostHIDDevice.c, built natively on
 * Linux by tests/host-hid-descriptor.py. It needs no device: descriptors are
 * parsed, and events are applied to devices mapped with made-up capabilities.
 *
 * The expected elements of the tablet and the keyboard descriptors are what
 * IOHIDManager showed on macOS 10.14.4 (x86_64, black-box, public API) for the
 * USB keyboard and tablet that QEMU emulates.
 */
#include "../src/HostHIDDescriptor.h"
#include "../src/HostHIDDevice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static const uint8_t tablet[] = {
	0x05,0x01,0x09,0x02,0xa1,0x01,0x09,0x01,0xa1,0x00,0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,
	0x95,0x01,0x75,0x03,0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x00,0x26,0xff,0x7f,0x35,0x00,0x46,0xff,0x7f,0x75,0x10,0x95,0x02,
	0x81,0x02,0x05,0x01,0x09,0x38,0x15,0x81,0x25,0x7f,0x35,0x00,0x45,0x00,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0 };

static const uint8_t keyboard[] = {
	0x05,0x01,0x09,0x06,0xa1,0x01,0x75,0x01,0x95,0x08,0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x81,0x02,0x95,0x01,0x75,0x08,
	0x81,0x01,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,
	0x25,0xff,0x05,0x07,0x19,0x00,0x29,0xff,0x81,0x00,0xc0 };

static const uint8_t f310[] = {
	0x05,0x01,0x09,0x04,0xa1,0x01,0xa1,0x02,0x75,0x08,0x95,0x04,0x15,0x00,0x26,0xff,0x00,0x35,0x00,0x46,0xff,0x00,0x09,0x30,0x09,0x31,
	0x09,0x32,0x09,0x35,0x81,0x02,0x75,0x04,0x95,0x01,0x25,0x07,0x46,0x3b,0x01,0x65,0x14,0x09,0x39,0x81,0x42,0x65,0x00,0x75,0x01,0x95,
	0x0c,0x25,0x01,0x45,0x01,0x05,0x09,0x19,0x01,0x29,0x0c,0x81,0x02,0x06,0x00,0xff,0x75,0x01,0x95,0x10,0x25,0x01,0x45,0x01,0x09,0x01,
	0x81,0x02,0xc0,0xa1,0x02,0x75,0x08,0x95,0x07,0x46,0xff,0x00,0x26,0xff,0x00,0x09,0x02,0x91,0x02,0xc0,0xc0 };

/* The element entry covering a cookie, or NULL */
static const struct iokitd_hid_element* entry_of(const struct iokitd_hid_descriptor* d, uint32_t cookie)
{
	for (size_t i = 0; i < d->element_count; i++)
		if (cookie >= d->elements[i].cookie_min && cookie <= d->elements[i].cookie_max)
			return &d->elements[i];
	return NULL;
}

static void test_tablet(void)
{
	struct iokitd_hid_descriptor d;
	const struct iokitd_hid_element* e;

	CHECK(iokitd_hid_parse(tablet, sizeof tablet, &d) == 0);
	/* macOS: collection 1 (application), 2 (physical), buttons 3 .. 7, X 8, Y 9, wheel 10 */
	CHECK(d.last_cookie == 11); /* the report handler is cookie 11 */
	e = entry_of(&d, 1);
	CHECK(e && e->kind == IOKITD_HID_COLLECTION && e->collection_type == 1 && e->usage_page == 1 && e->usage_min == 2);
	e = entry_of(&d, 2);
	CHECK(e && e->kind == IOKITD_HID_COLLECTION && e->collection_type == 0 && e->parent == 1 && e->usage_min == 1);
	e = entry_of(&d, 3);
	CHECK(e && e->cookie_min == 3 && e->cookie_max == 7 && e->usage_page == 9 && e->usage_min == 1 && e->usage_max == 5 && e->parent == 2);
	CHECK(e && e->report_size == 1 && e->report_count == 1 && e->logical_min == 0 && e->logical_max == 1 && e->physical_max == 1);
	e = entry_of(&d, 8);
	CHECK(e && e->cookie_min == 8 && e->cookie_max == 9 && e->usage_min == 0x30 && e->usage_max == 0x31 && e->report_size == 16);
	CHECK(e && e->logical_max == 32767 && e->physical_max == 32767);
	e = entry_of(&d, 10);
	CHECK(e && e->usage_min == 0x38 && e->logical_min == -127 && e->logical_max == 127 && e->physical_min == -127 && e->physical_max == 127);
	CHECK(e && (e->flags & IOKITD_HID_FLAG_RELATIVE) && e->bit_offset == 8 + 32);
	e = entry_of(&d, 11);
	CHECK(e && e->kind == IOKITD_HID_HANDLER && e->report_count == 6);
	CHECK(d.pair_count == 1 && d.pairs[0].page == 1 && d.pairs[0].usage == 2);
	CHECK(iokitd_hid_report_bytes(&d, IOKITD_HID_INPUT, 0) == 6 && iokitd_hid_max_report_bytes(&d, IOKITD_HID_INPUT) == 6);
	iokitd_hid_free(&d);
}

static void test_keyboard(void)
{
	struct iokitd_hid_descriptor d;
	const struct iokitd_hid_element* e;

	CHECK(iokitd_hid_parse(keyboard, sizeof keyboard, &d) == 0);
	/* macOS: 2 .. 9 modifiers, 10 .. 15 slots, 16 report handler, 17 .. 272 usages 0 .. 255, 273 .. 277 LEDs */
	CHECK(d.last_cookie == 277);
	e = entry_of(&d, 2);
	CHECK(e && e->cookie_max == 9 && e->usage_min == 0xe0 && e->usage_max == 0xe7 && e->report_size == 1 && e->report_count == 1 && !e->duplicates);
	e = entry_of(&d, 10);
	CHECK(e && e->array == IOKITD_HID_ARRAY_SLOTS && e->cookie_max == 15 && e->report_size == 8 && e->report_count == 6 && e->duplicates);
	CHECK(e && e->usage_min == 0xffffffffu && e->logical_min == 0 && e->logical_max == -1 && e->physical_max == -1 && e->bit_offset == 16);
	e = entry_of(&d, 16);
	CHECK(e && e->kind == IOKITD_HID_HANDLER && e->report_count == 8);
	e = entry_of(&d, 17);
	CHECK(e && e->array == IOKITD_HID_ARRAY_USAGES && e->cookie_max == 272 && e->usage_min == 0 && e->usage_max == 255 && e->report_size == 1);
	CHECK(e && e->logical_min == 0 && e->logical_max == 1 && e->slot_bit_offset == 16 && e->slot_size == 8 && e->slot_count == 6);
	e = entry_of(&d, 273);
	CHECK(e && e->kind == IOKITD_HID_OUTPUT && e->cookie_max == 277 && e->usage_page == 8 && e->usage_min == 1 && e->usage_max == 5);
	CHECK(iokitd_hid_report_bytes(&d, IOKITD_HID_OUTPUT, 0) == 1 && iokitd_hid_max_report_bytes(&d, IOKITD_HID_OUTPUT) == 1);
	iokitd_hid_free(&d);
}

static void test_f310(void)
{
	struct iokitd_hid_descriptor d;
	const struct iokitd_hid_element* e;

	CHECK(iokitd_hid_parse(f310, sizeof f310, &d) == 0);
	CHECK(d.pair_count == 1 && d.pairs[0].page == 1 && d.pairs[0].usage == 4);
	e = entry_of(&d, 2);
	CHECK(e && e->kind == IOKITD_HID_COLLECTION && e->collection_type == 2 && e->parent == 1);
	e = entry_of(&d, 3);
	CHECK(e && e->cookie_max == 5 && e->usage_min == 0x30 && e->logical_max == 255 && e->physical_max == 255 && e->report_size == 8);
	e = entry_of(&d, 7);
	CHECK(e && e->usage_min == 0x39 && e->report_size == 4 && e->logical_min == 0 && e->logical_max == 7 && e->physical_max == 315);
	CHECK(e && e->unit == 0x14 && (e->flags & IOKITD_HID_FLAG_NULL_STATE) && e->bit_offset == 32);
	e = entry_of(&d, 8);
	CHECK(e && e->cookie_max == 19 && e->usage_page == 9 && e->bit_offset == 36 && e->physical_max == 1);
	e = entry_of(&d, 20);
	CHECK(e && e->cookie_max == 35 && e->usage_page == 0xff00 && e->usage_min == 1 && e->usage_max == 1 && e->duplicates && e->bit_offset == 48);
	CHECK(e && e->size == 16 && e->report_size == 1 && e->report_count == 16);
	e = entry_of(&d, 36);
	CHECK(e && e->kind == IOKITD_HID_COLLECTION && e->parent == 1);
	e = entry_of(&d, 37);
	CHECK(e && e->kind == IOKITD_HID_HANDLER);
	e = entry_of(&d, 38);
	CHECK(e && e->kind == IOKITD_HID_OUTPUT && e->cookie_max == 44 && e->usage_page == 0xff00 && e->usage_min == 2 && e->parent == 36 && e->duplicates);
	CHECK(d.last_cookie == 44);
	CHECK(iokitd_hid_max_report_bytes(&d, IOKITD_HID_INPUT) == 8 && iokitd_hid_max_report_bytes(&d, IOKITD_HID_OUTPUT) == 7 &&
		iokitd_hid_max_report_bytes(&d, IOKITD_HID_FEATURE) == 0);
	iokitd_hid_free(&d);
}

static void test_report_ids(void)
{
	/* Two input reports (IDs 1 and 2), an output report (3) and a feature report (4) */
	static const uint8_t ids[] = {
		0x05,0x01,0x09,0x05,0xa1,0x01, 0x85,0x01,0x09,0x30,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x02,
		0x85,0x02,0x09,0x31,0x15,0x00,0x26,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x02,
		0x85,0x03,0x06,0x00,0xff,0x09,0x20,0x75,0x08,0x95,0x04,0x91,0x02, 0x85,0x04,0x09,0x21,0x95,0x02,0xb1,0x02, 0xc0 };
	struct iokitd_hid_descriptor d;
	const struct iokitd_hid_element* e;

	CHECK(iokitd_hid_parse(ids, sizeof ids, &d) == 0);
	/* collection 1, X 2 (report 1), Y 3 (report 2), handlers 4 and 5, output 6 .. 9, feature 10 and 11 */
	e = entry_of(&d, 2);
	CHECK(e && e->report_id == 1 && e->logical_min == -127 && e->logical_max == 127 && e->bit_offset == 0);
	e = entry_of(&d, 3);
	CHECK(e && e->report_id == 2 && e->logical_max == 1023 && e->report_size == 16 && e->bit_offset == 0);
	e = entry_of(&d, 4);
	CHECK(e && e->kind == IOKITD_HID_HANDLER && e->report_id == 1 && e->report_count == 2);
	e = entry_of(&d, 5);
	CHECK(e && e->kind == IOKITD_HID_HANDLER && e->report_id == 2 && e->report_count == 3);
	e = entry_of(&d, 6);
	CHECK(e && e->kind == IOKITD_HID_OUTPUT && e->report_id == 3 && e->cookie_max == 9);
	e = entry_of(&d, 10);
	CHECK(e && e->kind == IOKITD_HID_FEATURE && e->report_id == 4 && e->cookie_max == 11);
	CHECK(iokitd_hid_report_bytes(&d, IOKITD_HID_INPUT, 2) == 2 && iokitd_hid_max_report_bytes(&d, IOKITD_HID_INPUT) == 3);
	CHECK(iokitd_hid_max_report_bytes(&d, IOKITD_HID_OUTPUT) == 5 && iokitd_hid_max_report_bytes(&d, IOKITD_HID_FEATURE) == 3);
	iokitd_hid_free(&d);
}

static void test_malformed(void)
{
	struct iokitd_hid_descriptor d;
	static const uint8_t truncated[] = { 0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x75 };
	static const uint8_t unbalanced[] = { 0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02 };
	static const uint8_t extra_end[] = { 0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xc0, 0xc0 };
	static const uint8_t no_items[] = { 0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xc0 };
	static const uint8_t huge[] = { 0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x77, 0xff, 0xff, 0xff, 0x7f, 0x95, 0x01, 0x81, 0x02, 0xc0 };

	CHECK(iokitd_hid_parse(truncated, sizeof truncated, &d) < 0);
	CHECK(iokitd_hid_parse(unbalanced, sizeof unbalanced, &d) < 0);
	CHECK(iokitd_hid_parse(extra_end, sizeof extra_end, &d) < 0);
	CHECK(iokitd_hid_parse(no_items, sizeof no_items, &d) < 0);
	CHECK(iokitd_hid_parse(huge, sizeof huge, &d) < 0);
	CHECK(iokitd_hid_parse(NULL, 0, &d) < 0);
}

static void set_bit(uint8_t* bits, unsigned n)
{
	bits[n / 8] |= (uint8_t) (1u << (n % 8));
}

static void test_f310_events(void)
{
	struct iokitd_hid_caps caps;
	struct iokitd_hid_device* dev;
	size_t bytes;
	const uint8_t* report;

	memset(&caps, 0, sizeof caps);
	for (unsigned c = 0x120; c < 0x120 + 12; c++)   /* BTN_JOYSTICK .. */
		set_bit(caps.keys, c);
	static const unsigned axes[] = { 0, 1, 2, 5, 0x10, 0x11 };
	for (size_t i = 0; i < sizeof axes / sizeof axes[0]; i++)
		set_bit(caps.abs, axes[i]);
	dev = iokitd_hid_device_create(f310, sizeof f310, &caps);
	CHECK(dev != NULL);
	if (!dev)
		return;
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(bytes == 8 && report[4] == 0x08); /* the hat is in its null state (8), nothing else is set */

	iokitd_hid_device_apply(dev, 3, 0, 0x80);
	iokitd_hid_device_apply(dev, 3, 1, 0x20);
	iokitd_hid_device_apply(dev, 3, 2, 0xe0);
	iokitd_hid_device_apply(dev, 3, 5, 0x40);
	iokitd_hid_device_apply(dev, 1, 0x120, 1);      /* button 1 */
	iokitd_hid_device_apply(dev, 1, 0x122, 1);      /* button 3 */
	iokitd_hid_device_apply(dev, 1, 0x124, 1);      /* button 5 */
	iokitd_hid_device_apply(dev, 1, 0x12b, 1);      /* button 12 */
	iokitd_hid_device_apply(dev, 3, 0x10, 1);       /* hat x right */
	iokitd_hid_device_apply(dev, 3, 0x11, -1);      /* hat y up: up-right is 1 */
	CHECK(iokitd_hid_device_commit(dev, NULL, NULL) == 1);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(report[0] == 0x80 && report[1] == 0x20 && report[2] == 0xe0 && report[3] == 0x40);
	CHECK(report[4] == 0x51 && report[5] == 0x81 && report[6] == 0 && report[7] == 0);

	static const struct { int x, y, value; } hat[] = {
		{ 0, -1, 0 }, { 1, -1, 1 }, { 1, 0, 2 }, { 1, 1, 3 }, { 0, 1, 4 }, { -1, 1, 5 }, { -1, 0, 6 }, { -1, -1, 7 }, { 0, 0, 8 } };
	for (size_t i = 0; i < sizeof hat / sizeof hat[0]; i++)
	{
		iokitd_hid_device_apply(dev, 3, 0x10, hat[i].x);
		iokitd_hid_device_apply(dev, 3, 0x11, hat[i].y);
		iokitd_hid_device_commit(dev, NULL, NULL);
		report = iokitd_hid_device_input(dev, 0, &bytes);
		CHECK((report[4] & 0x0f) == hat[i].value);
	}
	/* a commit without a change calls nobody */
	CHECK(iokitd_hid_device_commit(dev, NULL, NULL) == 0);
	iokitd_hid_device_free(dev);

	/* a node that offers nothing the descriptor uses is not described by it */
	memset(&caps, 0, sizeof caps);
	CHECK(iokitd_hid_device_create(f310, sizeof f310, &caps) == NULL);
}

static void test_keyboard_events(void)
{
	struct iokitd_hid_caps caps;
	struct iokitd_hid_device* dev;
	size_t bytes;
	const uint8_t* report;

	memset(&caps, 0, sizeof caps);
	for (unsigned c = 1; c < 128; c++)
		set_bit(caps.keys, c);
	dev = iokitd_hid_device_create(keyboard, sizeof keyboard, &caps);
	CHECK(dev != NULL);
	if (!dev)
		return;
	iokitd_hid_device_apply(dev, 1, 30, 1);         /* KEY_A, usage 4 */
	iokitd_hid_device_apply(dev, 1, 42, 1);         /* KEY_LEFTSHIFT, usage 0xe1 */
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(bytes == 8 && report[0] == 0x02 && report[1] == 0 && report[2] == 0x04 && report[3] == 0);
	iokitd_hid_device_apply(dev, 1, 48, 1);         /* KEY_B, usage 5 */
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(report[2] == 0x04 && report[3] == 0x05);
	iokitd_hid_device_apply(dev, 1, 30, 0);
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(report[2] == 0x05 && report[3] == 0);
	/* seven keys down in six slots: every slot reads ErrorRollOver (1) */
	for (unsigned c = 16; c < 23; c++)
		iokitd_hid_device_apply(dev, 1, c, 1);
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(report[2] == 1 && report[7] == 1);
	iokitd_hid_device_free(dev);

	uint8_t leds;
	struct iokitd_hid_descriptor d;
	CHECK(iokitd_hid_parse(keyboard, sizeof keyboard, &d) == 0);
	static const uint8_t caps_lock[1] = { 0x02 };
	CHECK(iokitd_hid_output_leds(&d, 0, caps_lock, 1, &leds) == 1 && leds == 0x02);
	CHECK(iokitd_hid_output_leds(&d, 7, caps_lock, 1, &leds) == 0);
	iokitd_hid_free(&d);
}

static void test_mouse_events(void)
{
	/* Buttons, relative X and Y of 16 bits, and a wheel of 8 bits */
	static const uint8_t mouse[] = {
		0x05,0x01,0x09,0x02,0xa1,0x01,0x09,0x01,0xa1,0x00,0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,0x01,0x81,0x02,
		0x95,0x01,0x75,0x05,0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x00,0x80,0x26,0xff,0x7f,0x75,0x10,0x95,0x02,0x81,0x06,
		0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0 };
	struct iokitd_hid_caps caps;
	struct iokitd_hid_device* dev;
	size_t bytes;
	const uint8_t* report;

	memset(&caps, 0, sizeof caps);
	for (unsigned c = 0x110; c < 0x113; c++)
		set_bit(caps.keys, c);
	set_bit(caps.rel, 0);
	set_bit(caps.rel, 1);
	set_bit(caps.rel, 8);
	dev = iokitd_hid_device_create(mouse, sizeof mouse, &caps);
	CHECK(dev != NULL);
	if (!dev)
		return;
	iokitd_hid_device_apply(dev, 1, 0x110, 1);
	iokitd_hid_device_apply(dev, 2, 0, 5);
	iokitd_hid_device_apply(dev, 2, 0, 2);          /* the motion of a frame adds up */
	iokitd_hid_device_apply(dev, 2, 1, -3);
	iokitd_hid_device_apply(dev, 2, 8, -1);
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(bytes == 6 && report[0] == 1 && report[1] == 7 && report[2] == 0 && report[3] == 0xfd && report[4] == 0xff && report[5] == 0xff);
	/* the next frame starts from no motion */
	iokitd_hid_device_apply(dev, 1, 0x111, 1);
	iokitd_hid_device_commit(dev, NULL, NULL);
	report = iokitd_hid_device_input(dev, 0, &bytes);
	CHECK(report[0] == 3 && report[1] == 0 && report[3] == 0 && report[5] == 0);
	iokitd_hid_device_free(dev);
}

/* sysfs <root> <event name>: prints the length of the descriptor and the nodes that share its HID device */
static int sysfs_mode(const char* root, const char* name)
{
	uint8_t* descriptor;
	size_t size;
	char hid_dir[1024], names[8][32];

	if (iokitd_hid_sysfs_descriptor(root, name, &descriptor, &size, hid_dir, sizeof hid_dir) < 0)
	{
		printf("none\n");
		return 0;
	}
	int count = iokitd_hid_sysfs_siblings(hid_dir, names, 8);
	printf("descriptor %zu bytes first %02x siblings", size, descriptor[0]);
	for (int i = 0; i < count; i++)
		printf(" %s", names[i]);
	printf("\n");
	free(descriptor);
	return 0;
}

int main(int argc, char** argv)
{
	if (argc == 4 && strcmp(argv[1], "sysfs") == 0)
		return sysfs_mode(argv[2], argv[3]);

	test_tablet();
	test_keyboard();
	test_f310();
	test_report_ids();
	test_malformed();
	test_f310_events();
	test_keyboard_events();
	test_mouse_events();
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("descriptor: ok\n");
	return 0;
}
