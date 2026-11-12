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

/* The Button page usage of a key code, or 0 if it is not a controller button */
static uint16_t button_usage(int code)
{
	if (code >= BTN_GAMEPAD && code <= BTN_THUMBR)
		return code - BTN_GAMEPAD + 1;
	if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
		return code - BTN_DPAD_UP + 16;
	if (code >= BTN_JOYSTICK && code < BTN_GAMEPAD)
		return code - BTN_JOYSTICK + 20;
	return 0;
}

/* The usage page and usage of an absolute axis, or 0 if it is not mapped (the hat is separate) */
static int axis_usage(int code, uint16_t* page, uint16_t* usage)
{
	switch (code)
	{
		case ABS_X: case ABS_Y: case ABS_Z: case ABS_RX: case ABS_RY: case ABS_RZ:
			*page = 0x01;
			*usage = 0x30 + code;
			return 1;
		case ABS_THROTTLE: *page = 0x02; *usage = 0xbb; return 1;
		case ABS_RUDDER: *page = 0x02; *usage = 0xba; return 1;
		case ABS_GAS: *page = 0x02; *usage = 0xc4; return 1;
		case ABS_BRAKE: *page = 0x02; *usage = 0xc5; return 1;
	}
	return 0;
}

static int cmp_axis(const void* a, const void* b)
{
	const struct iokitd_gamepad_axis* x = a;
	const struct iokitd_gamepad_axis* y = b;
	if (x->usage_page != y->usage_page)
		return x->usage_page < y->usage_page ? -1 : 1;
	return (int) x->usage - (int) y->usage;
}

/* Fixes the report layout once buttons, hat and axes are known */
static void layout_report(struct iokitd_gamepad* pad)
{
	size_t offset = (pad->button_count + 7) / 8;

	if (pad->has_hat)
	{
		pad->hat_byte_offset = (uint16_t) offset;
		offset += 1;
	}
	for (int i = 0; i < pad->axis_count; i++)
	{
		pad->axes[i].byte_offset = (uint16_t) offset;
		offset += pad->axes[i].bits / 8;
	}
	pad->report_size = (uint16_t) offset;
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

	for (int code = BTN_JOYSTICK; code < KEY_CNT; code++)
	{
		uint16_t usage = button_usage(code);

		if (usage == 0 || !bit_is_set(key_bits, code))
			continue;
		if (code >= BTN_GAMEPAD && code <= BTN_THUMBR)
			pad->has_gamepad_buttons = 1;
		if (pad->button_count < IOKITD_GAMEPAD_MAX_BUTTONS)
		{
			pad->buttons[pad->button_count].code = (uint16_t) code;
			pad->buttons[pad->button_count].usage = usage;
			pad->button_count++;
		}
	}

	if (pad->button_count == 0)
	{
		close(fd);
		return -1;
	}

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

		if (!axis_usage(code, &page, &usage) || pad->axis_count >= IOKITD_GAMEPAD_MAX_AXES || info.maximum <= info.minimum)
			continue;

		struct iokitd_gamepad_axis* axis = &pad->axes[pad->axis_count++];
		axis->code = (uint16_t) code;
		axis->usage_page = page;
		axis->usage = usage;
		axis->minimum = info.minimum;
		axis->maximum = info.maximum;
		axis->bits = (info.minimum < -32768 || info.maximum > (info.minimum < 0 ? 32767 : 65535)) ? 32 : 16;
	}
	/* A hat needs both of its axes */
	pad->has_hat = bit_is_set(abs_bits, ABS_HAT0X) && bit_is_set(abs_bits, ABS_HAT0Y)
		&& pad->hat_x_max > pad->hat_x_min && pad->hat_y_max > pad->hat_y_min;

	qsort(pad->axes, pad->axis_count, sizeof(pad->axes[0]), cmp_axis);
	layout_report(pad);

	if (ioctl(fd, EVIOCGID, id) == 0)
	{
		pad->bustype = id[0];
		pad->vendor = id[1];
		pad->product = id[2];
		pad->version = id[3];
	}
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
 * Hat values: 1 = up, 2 = up-right, ... 8 = up-left (clockwise), 0 = null,
 * as the report descriptor's logical range 1..8 with a null state declares.
 */
static uint8_t hat_report_value(int x, int y)
{
	static const uint8_t table[3][3] = {
		/* y = -1 (up) */ { 8, 1, 2 },
		/* y =  0      */ { 7, 0, 3 },
		/* y = +1 */      { 6, 5, 4 },
	};
	return table[y + 1][x + 1];
}

int iokitd_gamepad_apply_event(const struct iokitd_gamepad* pad, uint8_t* report,
	uint16_t type, uint16_t code, int32_t value)
{
	uint8_t before[IOKITD_GAMEPAD_MAX_BUTTONS / 8 + 1 + 1 + IOKITD_GAMEPAD_MAX_AXES * 4];
	size_t size = pad->report_size;

	if (size > sizeof(before))
		return 0;
	memcpy(before, report, size);

	if (type == EV_KEY)
	{
		for (int i = 0; i < pad->button_count; i++)
		{
			if (pad->buttons[i].code == code)
			{
				set_bit(report, i, value != 0);
				break;
			}
		}
	}
	else if (type == EV_ABS)
	{
		if (pad->has_hat && (code == ABS_HAT0X || code == ABS_HAT0Y))
		{
			/* The hat axes arrive one at a time; keep the other from the report */
			uint8_t current = report[pad->hat_byte_offset] & 0x0f;
			static const int8_t x_of[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
			static const int8_t y_of[9] = { 0, -1, -1, 0, 1, 1, 1, 0, -1 };
			int x = x_of[current], y = y_of[current];

			if (code == ABS_HAT0X)
				x = hat_axis_sign(value, pad->hat_x_min, pad->hat_x_max);
			else
				y = hat_axis_sign(value, pad->hat_y_min, pad->hat_y_max);
			report[pad->hat_byte_offset] = hat_report_value(x, y);
		}
		else
		{
			for (int i = 0; i < pad->axis_count; i++)
			{
				const struct iokitd_gamepad_axis* axis = &pad->axes[i];

				if (axis->code != code)
					continue;
				if (value < axis->minimum)
					value = axis->minimum;
				if (value > axis->maximum)
					value = axis->maximum;
				if (axis->bits == 16)
				{
					uint16_t v = (uint16_t) value;
					report[axis->byte_offset] = (uint8_t) v;
					report[axis->byte_offset + 1] = (uint8_t) (v >> 8);
				}
				else
				{
					uint32_t v = (uint32_t) value;
					for (int b = 0; b < 4; b++)
						report[axis->byte_offset + b] = (uint8_t) (v >> (8 * b));
				}
				break;
			}
		}
	}

	return memcmp(before, report, size) != 0;
}

void iokitd_gamepad_initial_report(int fd, const struct iokitd_gamepad* pad, uint8_t* report)
{
	uint8_t keys[KEY_CNT / 8] = {0};

	memset(report, 0, pad->report_size);

	/* An axis at rest is not at zero: start from what the kernel reports */
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
		for (int i = 0; i < pad->button_count; i++)
			set_bit(report, i, get_bit(keys, pad->buttons[i].code));
	}
}

int iokitd_gamepad_read(int fd, const struct iokitd_gamepad* pad, uint8_t* report)
{
	struct evdev_event events[32];
	int changed = 0;
	/* Applied to a copy until SYN_REPORT, so a half-delivered frame is never visible */
	uint8_t pending[IOKITD_GAMEPAD_MAX_BUTTONS / 8 + 1 + 1 + IOKITD_GAMEPAD_MAX_AXES * 4];

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
#define LOGICAL_MIN(w, v) put_item(w, 0x14, v)
#define LOGICAL_MAX(w, v) put_item(w, 0x24, v)
#define REPORT_SIZE(w, v) put_item(w, 0x74, v)
#define REPORT_COUNT(w, v) put_item(w, 0x94, v)
#define INPUT(w, v) put_item(w, 0x80, v)

int iokitd_gamepad_report_descriptor(const struct iokitd_gamepad* pad, uint8_t* out, size_t size)
{
	struct descriptor_writer w = { out, size, 0, 0 };

	USAGE_PAGE(&w, 0x01);
	USAGE(&w, iokitd_gamepad_primary_usage(pad));
	put_item(&w, 0xa0, 0x01);       /* Collection (Application) */

	/* Buttons are not necessarily consecutive usages; one Usage item each */
	USAGE_PAGE(&w, 0x09);
	for (int i = 0; i < pad->button_count; i++)
		USAGE(&w, pad->buttons[i].usage);
	LOGICAL_MIN(&w, 0);
	LOGICAL_MAX(&w, 1);
	REPORT_SIZE(&w, 1);
	REPORT_COUNT(&w, pad->button_count);
	INPUT(&w, 0x02);                /* Data, Variable, Absolute */
	if (pad->button_count % 8)
	{
		REPORT_SIZE(&w, 1);
		REPORT_COUNT(&w, 8 - pad->button_count % 8);
		INPUT(&w, 0x03);            /* Constant */
	}

	if (pad->has_hat)
	{
		USAGE_PAGE(&w, 0x01);
		USAGE(&w, 0x39);
		LOGICAL_MIN(&w, 1);
		LOGICAL_MAX(&w, 8);
		put_item(&w, 0x34, 0);      /* Physical Minimum 0 */
		put_item(&w, 0x44, 315);    /* Physical Maximum 315 */
		put_item(&w, 0x64, 0x14);   /* Unit: degrees */
		REPORT_SIZE(&w, 4);
		REPORT_COUNT(&w, 1);
		INPUT(&w, 0x42);            /* Data, Variable, Absolute, Null state */
		put_item(&w, 0x64, 0);      /* Unit: none */
		REPORT_SIZE(&w, 4);
		REPORT_COUNT(&w, 1);
		INPUT(&w, 0x03);
	}

	for (int i = 0; i < pad->axis_count; i++)
	{
		const struct iokitd_gamepad_axis* axis = &pad->axes[i];

		USAGE_PAGE(&w, axis->usage_page);
		USAGE(&w, axis->usage);
		LOGICAL_MIN(&w, axis->minimum);
		LOGICAL_MAX(&w, axis->maximum);
		REPORT_SIZE(&w, axis->bits);
		REPORT_COUNT(&w, 1);
		INPUT(&w, 0x02);
	}

	put_byte(&w, 0xc0);             /* End Collection */

	return w.overflow ? -1 : (int) w.used;
}

#else /* !__LP64__ */

int iokitd_gamepad_probe(const char* path, struct iokitd_gamepad* pad) { return -1; }
int iokitd_gamepad_list(const char* directory, char paths[][64], int max) { return 0; }
int iokitd_gamepad_open(const char* path) { return -1; }
int iokitd_gamepad_read(int fd, const struct iokitd_gamepad* pad, uint8_t* report) { return -1; }
void iokitd_gamepad_initial_report(int fd, const struct iokitd_gamepad* pad, uint8_t* report) {}
int iokitd_gamepad_apply_event(const struct iokitd_gamepad* pad, uint8_t* report, uint16_t type, uint16_t code, int32_t value) { return 0; }
int iokitd_gamepad_report_descriptor(const struct iokitd_gamepad* pad, uint8_t* out, size_t size) { return -1; }

#endif
