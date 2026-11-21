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

#include "HostGamepad.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Linux input ABI */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define SYN_REPORT 0
#define SYN_DROPPED 3

#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_Z 0x02
#define ABS_RX 0x03
#define ABS_RY 0x04
#define ABS_RZ 0x05
#define ABS_THROTTLE 0x06
#define ABS_RUDDER 0x07
#define ABS_GAS 0x09
#define ABS_BRAKE 0x0a
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11
#define ABS_MT_SLOT 0x2f
#define ABS_CNT 0x40

#define BTN_JOYSTICK 0x120
#define BTN_GAMEPAD 0x130
#define BTN_SOUTH 0x130
#define BTN_EAST 0x131
#define BTN_NORTH 0x133
#define BTN_WEST 0x134
#define BTN_TL 0x136
#define BTN_TR 0x137
#define BTN_TL2 0x138
#define BTN_TR2 0x139
#define BTN_SELECT 0x13a
#define BTN_START 0x13b
#define BTN_MODE 0x13c
#define BTN_THUMBL 0x13d
#define BTN_THUMBR 0x13e
#define BTN_TOUCH 0x14a
#define BTN_DPAD_UP 0x220
#define BTN_DPAD_RIGHT 0x223
#define KEY_CNT 0x300

#define INPUT_PROP_ACCELEROMETER 0x06
#define INPUT_PROP_DIRECT 0x01
#define INPUT_PROP_POINTER 0x00
#define INPUT_PROP_CNT 0x20

#define IOC_READ_DIR 2u
#define IOC_ENCODE(dir, type, nr, size) \
	(((unsigned long)(dir) << 30) | ((unsigned long)(size) << 16) | ((unsigned long)(type) << 8) | (unsigned long)(nr))
#define EVIOCGID IOC_ENCODE(IOC_READ_DIR, 'E', 0x02, 8)
#define EVIOCGNAME(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x06, len)
#define EVIOCGUNIQ(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x08, len)
#define EVIOCGPROP(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x09, len)
#define EVIOCGKEY(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x18, len)
#define EVIOCGBIT(ev, len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x20 + (ev), len)
#define EVIOCGABS(abs) IOC_ENCODE(IOC_READ_DIR, 'E', 0x40 + (abs), 24)

#if defined(__LP64__)

struct evdev_event
{
	int64_t seconds, microseconds;
	uint16_t type, code;
	int32_t value;
};

struct evdev_absinfo
{
	int32_t value, minimum, maximum, fuzz, flat, resolution;
};

static int bit_is_set(const uint8_t* bits, int bit)
{
	return (bits[bit / 8] >> (bit % 8)) & 1;
}

static void copy_string(char* to, size_t size, const char* from)
{
	snprintf(to, size, "%s", from);
}

#define VENDOR_SONY 0x054c

/* The DualShock 4 (USB, Bluetooth, the wireless adapter) */
static int is_playstation_pad(uint16_t vendor, uint16_t product)
{
	return vendor == VENDOR_SONY && (product == 0x05c4 || product == 0x09cc || product == 0x0ba0);
}

/*
 * The fixed Button page usage of a key code in a layout, or 0 if the layout
 * gives it none. fixed_usages is how many usages the layout declares at
 * least (the extras follow them).
 */
static uint16_t button_usage(int layout, int code, int* fixed_usages)
{
	switch (layout)
	{
		case IOKITD_GAMEPAD_LAYOUT_PLAYSTATION:
			*fixed_usages = 14;
			switch (code)
			{
				case BTN_WEST: return 1;
				case BTN_SOUTH: return 2;
				case BTN_EAST: return 3;
				case BTN_NORTH: return 4;
				case BTN_TL: return 5;
				case BTN_TR: return 6;
				case BTN_TL2: return 7;
				case BTN_TR2: return 8;
				case BTN_SELECT: return 9;
				case BTN_START: return 10;
				case BTN_THUMBL: return 11;
				case BTN_THUMBR: return 12;
				case BTN_MODE: return 13;
			}
			return 0;
		case IOKITD_GAMEPAD_LAYOUT_STANDARD:
			*fixed_usages = 15;
			return (code >= BTN_GAMEPAD && code <= BTN_THUMBR) ? code - BTN_GAMEPAD + 1 : 0;
		default:
			*fixed_usages = 0;
			return (code >= BTN_JOYSTICK && code < BTN_GAMEPAD) ? code - BTN_JOYSTICK + 1 : 0;
	}
}

/* Whether a key code is one that is published as a controller button at all */
static int is_controller_button(int code)
{
	return (code >= BTN_JOYSTICK && code <= BTN_THUMBR) || (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT);
}

/* The trigger roles of the gamepad layouts */
enum { NOT_A_TRIGGER, LEFT_TRIGGER, RIGHT_TRIGGER };

/*
 * The usage page and usage of an absolute axis in a layout, or 0 if it is
 * not mapped (the hat is separate). right_stick_rxry says whether ABS_RX and
 * ABS_RY (rather than ABS_Z and ABS_RZ) are the right stick of a gamepad.
 */
static int axis_usage(int layout, int right_stick_rxry, int code, uint16_t* page, uint16_t* usage)
{
	*page = 0x01;
	if (layout == IOKITD_GAMEPAD_LAYOUT_JOYSTICK)
	{
		if (code >= ABS_X && code <= ABS_RZ)
		{
			*usage = 0x30 + code;
			return 1;
		}
	}
	else
	{
		int trigger = NOT_A_TRIGGER;

		switch (code)
		{
			case ABS_X: *usage = 0x30; return 1;
			case ABS_Y: *usage = 0x31; return 1;
			case ABS_RX: if (right_stick_rxry) { *usage = 0x32; return 1; } break;
			case ABS_RY: if (right_stick_rxry) { *usage = 0x35; return 1; } break;
			case ABS_Z:
				if (right_stick_rxry) trigger = LEFT_TRIGGER; else { *usage = 0x32; return 1; }
				break;
			case ABS_RZ:
				if (right_stick_rxry) trigger = RIGHT_TRIGGER; else { *usage = 0x35; return 1; }
				break;
			case ABS_BRAKE: trigger = LEFT_TRIGGER; break;
			case ABS_GAS: trigger = RIGHT_TRIGGER; break;
		}
		if (trigger != NOT_A_TRIGGER)
		{
			if (layout == IOKITD_GAMEPAD_LAYOUT_PLAYSTATION)
			{
				*usage = trigger == LEFT_TRIGGER ? 0x33 : 0x34;
			}
			else
			{
				*page = 0x02;
				*usage = trigger == LEFT_TRIGGER ? 0xc5 : 0xc4;
			}
			return 1;
		}
	}

	switch (code)
	{
		case ABS_THROTTLE: *page = 0x02; *usage = 0xbb; return 1;
		case ABS_RUDDER: *page = 0x02; *usage = 0xba; return 1;
	}
	if (layout == IOKITD_GAMEPAD_LAYOUT_JOYSTICK)
	{
		switch (code)
		{
			case ABS_GAS: *page = 0x02; *usage = 0xc4; return 1;
			case ABS_BRAKE: *page = 0x02; *usage = 0xc5; return 1;
		}
	}
	return 0;
}

/* The order of axes within a layout's report: the sticks first, then the triggers and the rest */
static int axis_rank(const struct iokitd_gamepad_axis* a)
{
	if (a->usage_page == 0x01)
	{
		switch (a->usage)
		{
			case 0x30: return 0;
			case 0x31: return 1;
			case 0x32: return 2;
			case 0x35: return 3;
		}
		return 10 + a->usage;
	}
	if (a->usage_page == 0x02 && a->usage == 0xc5)
		return 4; /* Brake before Accelerator */
	return 100 + a->usage_page * 256 + a->usage;
}

static int cmp_axis(const void* a, const void* b)
{
	return axis_rank(a) - axis_rank(b);
}

static int bits_for_range(int64_t range)
{
	if (range <= 255)
		return 8;
	if (range <= 1023)
		return 10;
	if (range <= 65535)
		return 16;
	return 32;
}

static int is_playstation_trigger(const struct iokitd_gamepad_axis* a)
{
	return a->usage_page == 0x01 && (a->usage == 0x33 || a->usage == 0x34);
}

/* Appends an item to the report and fixes its position; the report is built up bit by bit */
static void add_field(struct iokitd_gamepad* pad, int kind, int index, int bits, int pad_to_byte, int* total)
{
	struct iokitd_gamepad_field* f = &pad->fields[pad->field_count++];

	f->kind = (uint8_t) kind;
	f->index = (uint8_t) index;
	f->bit_offset = (uint16_t) *total;
	f->bits = (uint8_t) bits;
	*total += bits;
	f->padding = pad_to_byte ? (uint8_t) ((8 - *total % 8) % 8) : 0;
	*total += f->padding;
}

void iokitd_gamepad_layout(struct iokitd_gamepad* pad)
{
	int total = 0;
	int playstation = pad->layout == IOKITD_GAMEPAD_LAYOUT_PLAYSTATION;

	pad->field_count = 0;
	/* The DualShock 4 report has its triggers after the buttons */
	for (int pass = 0; pass < 2; pass++)
	{
		if (pass == 1)
		{
			if (pad->has_hat)
				add_field(pad, IOKITD_GAMEPAD_FIELD_HAT, 0, 4, !playstation, &total);
			if (pad->button_bits)
				add_field(pad, IOKITD_GAMEPAD_FIELD_BUTTONS, 0, pad->button_bits, 1, &total);
		}
		for (int i = 0; i < pad->axis_count; i++)
		{
			int late = playstation && is_playstation_trigger(&pad->axes[i]);
			if (late == pass)
				add_field(pad, IOKITD_GAMEPAD_FIELD_AXIS, i, pad->axes[i].bits, 1, &total);
		}
	}
	pad->report_size = (uint16_t) ((total + 7) / 8);
}

int iokitd_gamepad_probe(const char* path, struct iokitd_gamepad* pad)
{
	uint8_t ev_bits[4] = {0}, abs_bits[ABS_CNT / 8] = {0}, key_bits[KEY_CNT / 8] = {0}, prop_bits[INPUT_PROP_CNT / 8] = {0};
	uint16_t id[4] = {0};
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return -1;

	memset(pad, 0, sizeof(*pad));
	copy_string(pad->path, sizeof(pad->path), path);

	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0
		|| ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0
		|| ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0
		|| !bit_is_set(ev_bits, EV_ABS) || !bit_is_set(ev_bits, EV_KEY))
	{
		close(fd);
		return -1;
	}
	/* The properties are optional: old kernels do not have the ioctl */
	ioctl(fd, EVIOCGPROP(sizeof(prop_bits)), prop_bits);

	/* Touch surfaces, tablets and the motion sensors of controllers share the axis codes */
	if (bit_is_set(abs_bits, ABS_MT_SLOT) || bit_is_set(key_bits, BTN_TOUCH)
		|| bit_is_set(prop_bits, INPUT_PROP_ACCELEROMETER) || bit_is_set(prop_bits, INPUT_PROP_DIRECT)
		|| !bit_is_set(abs_bits, ABS_X) || !bit_is_set(abs_bits, ABS_Y))
	{
		close(fd);
		return -1;
	}

	if (ioctl(fd, EVIOCGID, id) == 0)
	{
		pad->bustype = id[0];
		pad->vendor = id[1];
		pad->product = id[2];
		pad->version = id[3];
	}

	for (int code = BTN_GAMEPAD; code <= BTN_THUMBR; code++)
		pad->has_gamepad_buttons = pad->has_gamepad_buttons || bit_is_set(key_bits, code);
	pad->layout = !pad->has_gamepad_buttons ? IOKITD_GAMEPAD_LAYOUT_JOYSTICK
		: is_playstation_pad(pad->vendor, pad->product) ? IOKITD_GAMEPAD_LAYOUT_PLAYSTATION
		: IOKITD_GAMEPAD_LAYOUT_STANDARD;

	/* The buttons the layout has a usage for take it; the others follow in the order of their codes */
	int fixed = 0, next_extra, used_usages[IOKITD_GAMEPAD_MAX_BUTTONS + 64] = {0};
	button_usage(pad->layout, 0, &fixed);
	next_extra = fixed + 1;
	pad->button_bits = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		for (int code = BTN_JOYSTICK; code < KEY_CNT; code++)
		{
			int dummy;
			uint16_t usage;

			if (!is_controller_button(code) || !bit_is_set(key_bits, code))
				continue;
			usage = button_usage(pad->layout, code, &dummy);
			if ((pass == 0) != (usage != 0))
				continue;
			if (pass == 1)
			{
				if (next_extra <= pad->button_bits)
					next_extra = pad->button_bits + 1;
				while (next_extra < (int) (sizeof(used_usages) / sizeof(used_usages[0])) && used_usages[next_extra])
					next_extra++;
				usage = (uint16_t) next_extra++;
			}
			if (pad->button_count >= IOKITD_GAMEPAD_MAX_BUTTONS || usage > 64)
				continue;
			used_usages[usage] = 1;
			pad->buttons[pad->button_count].code = (uint16_t) code;
			pad->buttons[pad->button_count].usage = usage;
			pad->button_count++;
			if (usage > pad->button_bits)
				pad->button_bits = usage;
		}
	}

	if (pad->button_count == 0)
	{
		close(fd);
		return -1;
	}
	if (pad->button_bits < fixed)
		pad->button_bits = fixed;

	/* Without both ABS_RX and ABS_RY a gamepad has its right stick on ABS_Z and ABS_RZ */
	int right_stick_rxry = bit_is_set(abs_bits, ABS_RX) && bit_is_set(abs_bits, ABS_RY);

	for (int code = 0; code < ABS_CNT; code++)
	{
		struct evdev_absinfo info;
		uint16_t page, usage;

		if (!bit_is_set(abs_bits, code) || ioctl(fd, EVIOCGABS(code), &info) < 0)
			continue;

		if (code == ABS_HAT0X || code == ABS_HAT0Y)
		{
			if (code == ABS_HAT0X)
			{
				pad->hat_x_min = info.minimum;
				pad->hat_x_max = info.maximum;
			}
			else
			{
				pad->hat_y_min = info.minimum;
				pad->hat_y_max = info.maximum;
			}
			continue;
		}

		if (!axis_usage(pad->layout, right_stick_rxry, code, &page, &usage) || pad->axis_count >= IOKITD_GAMEPAD_MAX_AXES
			|| info.maximum <= info.minimum)
			continue;

		/* Two axes of the controller may map to one usage (ABS_BRAKE and ABS_Z); the first wins */
		int duplicate = 0;
		for (int i = 0; i < pad->axis_count; i++)
			duplicate = duplicate || (pad->axes[i].usage_page == page && pad->axes[i].usage == usage);
		if (duplicate)
			continue;

		struct iokitd_gamepad_axis* axis = &pad->axes[pad->axis_count++];
		axis->code = (uint16_t) code;
		axis->usage_page = page;
		axis->usage = usage;
		axis->minimum = info.minimum;
		axis->maximum = info.maximum;
		axis->bits = (uint8_t) bits_for_range((int64_t) info.maximum - info.minimum);
	}
	/* A hat needs both of its axes */
	pad->has_hat = bit_is_set(abs_bits, ABS_HAT0X) && bit_is_set(abs_bits, ABS_HAT0Y)
		&& pad->hat_x_max > pad->hat_x_min && pad->hat_y_max > pad->hat_y_min;
	pad->hat_logical_min = pad->layout == IOKITD_GAMEPAD_LAYOUT_PLAYSTATION ? 0 : 1;

	qsort(pad->axes, pad->axis_count, sizeof(pad->axes[0]), cmp_axis);
	iokitd_gamepad_layout(pad);

	if (ioctl(fd, EVIOCGNAME(sizeof(pad->name) - 1), pad->name) < 0)
		pad->name[0] = '\0';
	if (ioctl(fd, EVIOCGUNIQ(sizeof(pad->serial) - 1), pad->serial) < 0)
		pad->serial[0] = '\0';

	close(fd);
	return 0;
}

static int cmp_names(const void* a, const void* b)
{
	const char* x = a;
	const char* y = b;
	size_t lx = strlen(x), ly = strlen(y);
	/* event2 before event10 */
	if (lx != ly)
		return lx < ly ? -1 : 1;
	return strcmp(x, y);
}

int iokitd_gamepad_list(const char* directory, char paths[][64], int max)
{
	DIR* dir = opendir(directory);
	int count = 0;
	char names[256][32];
	struct dirent* entry;

	if (!dir)
		return 0;

	while (count < 256 && (entry = readdir(dir)) != NULL)
	{
		const char* name = entry->d_name;
		size_t length = strlen(name);

		if (strncmp(name, "event", 5) != 0 || length == 5 || length >= sizeof(names[0]))
			continue;
		int digits = 1;
		for (size_t i = 5; i < length; i++)
			digits = digits && name[i] >= '0' && name[i] <= '9';
		if (digits)
			strcpy(names[count++], name);
	}
	closedir(dir);

	qsort(names, count, sizeof(names[0]), cmp_names);
	if (count > max)
		count = max;
	for (int i = 0; i < count; i++)
		snprintf(paths[i], 64, "%s/%s", directory, names[i]);
	return count;
}

int iokitd_gamepad_open(const char* path)
{
	return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
}

/* Bit access in the report, least significant bit first within a byte, as a HID report is packed */
static void set_bit(uint8_t* report, int bit, int on)
{
	if (on)
		report[bit / 8] |= (uint8_t) (1u << (bit % 8));
	else
		report[bit / 8] &= (uint8_t) ~(1u << (bit % 8));
}

static int get_bit(const uint8_t* report, int bit)
{
	return (report[bit / 8] >> (bit % 8)) & 1;
}

static void put_bits(uint8_t* report, int bit, int bits, uint32_t value)
{
	for (int i = 0; i < bits; i++)
		set_bit(report, bit + i, (value >> i) & 1);
}

static uint32_t get_bits(const uint8_t* report, int bit, int bits)
{
	uint32_t value = 0;
	for (int i = 0; i < bits; i++)
		value |= (uint32_t) get_bit(report, bit + i) << i;
	return value;
}

/* The hat is stored as the two directions of each axis; the 4-bit report value is derived */
static int32_t hat_axis_sign(int32_t value, int32_t minimum, int32_t maximum)
{
	int32_t middle = minimum + (maximum - minimum) / 2;
	if (value < middle)
		return -1;
	if (value > middle)
		return 1;
	return 0;
}

/*
 * Hat directions, numbered 0 = centred, 1 = up, 2 = up-right, ... 8 = up-left
 * (clockwise). The report holds the number itself when the hat's logical
 * minimum is 1 (centred is 0, outside the range: the null state), and the
 * number minus one when it is 0 (centred is 8, again outside the range).
 */
static const int8_t hat_x_of[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
static const int8_t hat_y_of[9] = { 0, -1, -1, 0, 1, 1, 1, 0, -1 };

static const struct iokitd_gamepad_field* find_field(const struct iokitd_gamepad* pad, int kind)
{
	for (int i = 0; i < pad->field_count; i++)
	{
		if (pad->fields[i].kind == kind)
			return &pad->fields[i];
	}
	return NULL;
}

static uint32_t hat_report_value(const struct iokitd_gamepad* pad, int direction)
{
	if (pad->hat_logical_min == 1)
		return (uint32_t) direction;
	return direction == 0 ? 8 : (uint32_t) direction - 1;
}

static int hat_direction(const struct iokitd_gamepad* pad, uint32_t value)
{
	if (pad->hat_logical_min == 1)
		return value <= 8 ? (int) value : 0;
	return value <= 7 ? (int) value + 1 : 0;
}

/* The report with every item at rest: the hat centred, nothing else set */
static void rest_report(const struct iokitd_gamepad* pad, uint8_t* report)
{
	const struct iokitd_gamepad_field* hat = find_field(pad, IOKITD_GAMEPAD_FIELD_HAT);

	memset(report, 0, pad->report_size);
	if (hat)
		put_bits(report, hat->bit_offset, 4, hat_report_value(pad, 0));
}

int iokitd_gamepad_apply_event(const struct iokitd_gamepad* pad, uint8_t* report,
	uint16_t type, uint16_t code, int32_t value)
{
	uint8_t before[IOKITD_GAMEPAD_MAX_REPORT];
	size_t size = pad->report_size;

	if (size > sizeof(before))
		return 0;
	memcpy(before, report, size);

	if (type == EV_KEY)
	{
		const struct iokitd_gamepad_field* buttons = find_field(pad, IOKITD_GAMEPAD_FIELD_BUTTONS);

		for (int i = 0; buttons && i < pad->button_count; i++)
		{
			if (pad->buttons[i].code == code)
			{
				set_bit(report, buttons->bit_offset + pad->buttons[i].usage - 1, value != 0);
				break;
			}
		}
	}
	else if (type == EV_ABS)
	{
		const struct iokitd_gamepad_field* hat = find_field(pad, IOKITD_GAMEPAD_FIELD_HAT);

		if (hat && (code == ABS_HAT0X || code == ABS_HAT0Y))
		{
			/* The hat axes arrive one at a time; keep the other from the report */
			int current = hat_direction(pad, get_bits(report, hat->bit_offset, 4));
			int x = hat_x_of[current], y = hat_y_of[current];

			if (code == ABS_HAT0X)
				x = hat_axis_sign(value, pad->hat_x_min, pad->hat_x_max);
			else
				y = hat_axis_sign(value, pad->hat_y_min, pad->hat_y_max);

			int direction = 0;
			for (int d = 1; d <= 8; d++)
			{
				if (hat_x_of[d] == x && hat_y_of[d] == y)
					direction = d;
			}
			put_bits(report, hat->bit_offset, 4, hat_report_value(pad, direction));
		}
		else
		{
			for (int i = 0; i < pad->field_count; i++)
			{
				const struct iokitd_gamepad_field* f = &pad->fields[i];
				const struct iokitd_gamepad_axis* axis;

				if (f->kind != IOKITD_GAMEPAD_FIELD_AXIS)
					continue;
				axis = &pad->axes[f->index];
				if (axis->code != code)
					continue;
				if (value < axis->minimum)
					value = axis->minimum;
				if (value > axis->maximum)
					value = axis->maximum;
				put_bits(report, f->bit_offset, f->bits, (uint32_t) ((int64_t) value - axis->minimum));
				break;
			}
		}
	}

	return memcmp(before, report, size) != 0;
}

void iokitd_gamepad_initial_report(int fd, const struct iokitd_gamepad* pad, uint8_t* report)
{
	uint8_t keys[KEY_CNT / 8] = {0};

	rest_report(pad, report);

	/* An axis at rest is not at its minimum: start from what the kernel reports */
	for (int i = 0; i < pad->axis_count; i++)
	{
		struct evdev_absinfo info;
		if (ioctl(fd, EVIOCGABS(pad->axes[i].code), &info) == 0)
			iokitd_gamepad_apply_event(pad, report, EV_ABS, pad->axes[i].code, info.value);
	}
	if (pad->has_hat)
	{
		struct evdev_absinfo x, y;
		if (ioctl(fd, EVIOCGABS(ABS_HAT0X), &x) == 0)
			iokitd_gamepad_apply_event(pad, report, EV_ABS, ABS_HAT0X, x.value);
		if (ioctl(fd, EVIOCGABS(ABS_HAT0Y), &y) == 0)
			iokitd_gamepad_apply_event(pad, report, EV_ABS, ABS_HAT0Y, y.value);
	}
	if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) == 0)
	{
		const struct iokitd_gamepad_field* buttons = find_field(pad, IOKITD_GAMEPAD_FIELD_BUTTONS);

		for (int i = 0; buttons && i < pad->button_count; i++)
			set_bit(report, buttons->bit_offset + pad->buttons[i].usage - 1, get_bit(keys, pad->buttons[i].code));
	}
}

int iokitd_gamepad_read(int fd, const struct iokitd_gamepad* pad, uint8_t* report)
{
	struct evdev_event events[32];
	int changed = 0;
	/* Applied to a copy until SYN_REPORT, so a half-delivered frame is never visible */
	uint8_t pending[IOKITD_GAMEPAD_MAX_REPORT];

	if (pad->report_size > sizeof(pending))
		return 0;
	memcpy(pending, report, pad->report_size);

	for (;;)
	{
		ssize_t n = read(fd, events, sizeof(events));

		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				break;
			return -1; /* ENODEV: unplugged */
		}
		if (n == 0)
			return -1;

		for (size_t i = 0; i < (size_t) n / sizeof(events[0]); i++)
		{
			const struct evdev_event* e = &events[i];

			if (e->type == EV_SYN)
			{
				if (e->code == SYN_REPORT && memcmp(pending, report, pad->report_size) != 0)
				{
					memcpy(report, pending, pad->report_size);
					changed = 1;
				}
				else if (e->code == SYN_DROPPED)
				{
					/* The kernel's queue overflowed: events were lost, so resynchronise */
					iokitd_gamepad_initial_report(fd, pad, pending);
				}
			}
			else
				iokitd_gamepad_apply_event(pad, pending, e->type, e->code, e->value);
		}
	}
	return changed;
}

/* Short items of a HID report descriptor */
struct descriptor_writer
{
	uint8_t* out;
	size_t size, used;
	int overflow;
};

static void put_byte(struct descriptor_writer* w, uint8_t b)
{
	if (w->used < w->size)
		w->out[w->used] = b;
	else
		w->overflow = 1;
	w->used++;
}

static void put_item(struct descriptor_writer* w, uint8_t prefix, int32_t value)
{
	int bytes = (value >= -128 && value <= 127) ? 1 : (value >= -32768 && value <= 32767) ? 2 : 4;

	put_byte(w, (uint8_t) (prefix | (bytes == 4 ? 3 : bytes)));
	for (int i = 0; i < bytes; i++)
		put_byte(w, (uint8_t) ((uint32_t) value >> (8 * i)));
}

#define USAGE_PAGE(w, v) put_item(w, 0x04, v)
#define USAGE(w, v) put_item(w, 0x08, v)
#define USAGE_MIN(w, v) put_item(w, 0x18, v)
#define USAGE_MAX(w, v) put_item(w, 0x28, v)
#define LOGICAL_MIN(w, v) put_item(w, 0x14, v)
#define LOGICAL_MAX(w, v) put_item(w, 0x24, v)
#define REPORT_SIZE(w, v) put_item(w, 0x74, v)
#define REPORT_COUNT(w, v) put_item(w, 0x94, v)
#define INPUT(w, v) put_item(w, 0x80, v)

static void constant_padding(struct descriptor_writer* w, int bits)
{
	if (bits > 0)
	{
		REPORT_SIZE(w, 1);
		REPORT_COUNT(w, bits);
		INPUT(w, 0x03);                /* Constant */
	}
}

int iokitd_gamepad_report_descriptor(const struct iokitd_gamepad* pad, uint8_t* out, size_t size)
{
	struct descriptor_writer w = { out, size, 0, 0 };

	USAGE_PAGE(&w, 0x01);
	USAGE(&w, iokitd_gamepad_primary_usage(pad));
	put_item(&w, 0xa0, 0x01);       /* Collection (Application) */

	for (int i = 0; i < pad->field_count; i++)
	{
		const struct iokitd_gamepad_field* f = &pad->fields[i];

		switch (f->kind)
		{
			case IOKITD_GAMEPAD_FIELD_BUTTONS:
				USAGE_PAGE(&w, 0x09);
				USAGE_MIN(&w, 1);
				USAGE_MAX(&w, pad->button_bits);
				LOGICAL_MIN(&w, 0);
				LOGICAL_MAX(&w, 1);
				REPORT_SIZE(&w, 1);
				REPORT_COUNT(&w, pad->button_bits);
				INPUT(&w, 0x02);        /* Data, Variable, Absolute */
				break;
			case IOKITD_GAMEPAD_FIELD_HAT:
				USAGE_PAGE(&w, 0x01);
				USAGE(&w, 0x39);
				LOGICAL_MIN(&w, pad->hat_logical_min);
				LOGICAL_MAX(&w, pad->hat_logical_min + 7);
				put_item(&w, 0x34, 0);      /* Physical Minimum 0 */
				put_item(&w, 0x44, 315);    /* Physical Maximum 315 */
				put_item(&w, 0x64, 0x14);   /* Unit: degrees */
				REPORT_SIZE(&w, 4);
				REPORT_COUNT(&w, 1);
				INPUT(&w, 0x42);            /* Data, Variable, Absolute, Null state */
				put_item(&w, 0x34, 0);      /* The physical range and unit end with the hat */
				put_item(&w, 0x44, 0);
				put_item(&w, 0x64, 0);
				break;
			case IOKITD_GAMEPAD_FIELD_AXIS:
			{
				const struct iokitd_gamepad_axis* axis = &pad->axes[f->index];

				USAGE_PAGE(&w, axis->usage_page);
				USAGE(&w, axis->usage);
				LOGICAL_MIN(&w, 0);
				LOGICAL_MAX(&w, (int32_t) ((int64_t) axis->maximum - axis->minimum));
				REPORT_SIZE(&w, axis->bits);
				REPORT_COUNT(&w, 1);
				INPUT(&w, 0x02);
				break;
			}
		}
		constant_padding(&w, f->padding);
	}

	put_byte(&w, 0xc0);             /* End Collection */

	return w.overflow ? -1 : (int) w.used;
}

#else /* !__LP64__ */

void iokitd_gamepad_layout(struct iokitd_gamepad* pad) {}
int iokitd_gamepad_probe(const char* path, struct iokitd_gamepad* pad) { return -1; }
int iokitd_gamepad_list(const char* directory, char paths[][64], int max) { return 0; }
int iokitd_gamepad_open(const char* path) { return -1; }
int iokitd_gamepad_read(int fd, const struct iokitd_gamepad* pad, uint8_t* report) { return -1; }
void iokitd_gamepad_initial_report(int fd, const struct iokitd_gamepad* pad, uint8_t* report) {}
int iokitd_gamepad_apply_event(const struct iokitd_gamepad* pad, uint8_t* report, uint16_t type, uint16_t code, int32_t value) { return 0; }
int iokitd_gamepad_report_descriptor(const struct iokitd_gamepad* pad, uint8_t* out, size_t size) { return -1; }

#endif
