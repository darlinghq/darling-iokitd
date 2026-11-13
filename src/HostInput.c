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

#include "HostInput.h"
#include "HostGamepad.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Linux input ABI (linux/input-event-codes.h), spelled out as in HostGamepad.c */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define SYN_REPORT 0
#define SYN_DROPPED 3

#define REL_X 0x00
#define REL_Y 0x01
#define REL_HWHEEL 0x06
#define REL_WHEEL 0x08
#define REL_CNT 0x10

#define KEY_ENTER 28
#define KEY_SPACE 57
#define BTN_LEFT 0x110
#define BTN_EXTRA 0x114
#define KEY_CNT 0x300

#define IOC_READ_DIR 2u
#define IOC_ENCODE(dir, type, nr, size) \
	(((unsigned long)(dir) << 30) | ((unsigned long)(size) << 16) | ((unsigned long)(type) << 8) | (unsigned long)(nr))
#define EVIOCGID IOC_ENCODE(IOC_READ_DIR, 'E', 0x02, 8)
#define EVIOCGNAME(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x06, len)
#define EVIOCGUNIQ(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x08, len)
#define EVIOCGKEY(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x18, len)
#define EVIOCGBIT(ev, len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x20 + (ev), len)

#if defined(__LP64__)

struct evdev_event
{
	int64_t seconds, microseconds;
	uint16_t type, code;
	int32_t value;
};

/*
 * Keyboard/Keypad page usage of the evdev key codes 0 .. 127 and, from 183,
 * F13 .. F24, written out by key position (HID Usage Tables, section
 * "Keyboard/Keypad Page"). Zero means no usage.
 */
static const uint8_t key_usage_low[128] = {
	/*   0 */ 0x00, 0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
	/*   8 */ 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b,
	/*  16 */ 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c,
	/*  24 */ 0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16,
	/*  32 */ 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33,
	/*  40 */ 0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19,
	/*  48 */ 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55,
	/*  56 */ 0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
	/*  64 */ 0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f,
	/*  72 */ 0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59,
	/*  80 */ 0x5a, 0x5b, 0x62, 0x63, 0x00, 0x00, 0x64, 0x44,
	/*  88 */ 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/*  96 */ 0x58, 0xe4, 0x54, 0x46, 0xe6, 0x00, 0x4a, 0x52,
	/* 104 */ 0x4b, 0x50, 0x4f, 0x4d, 0x51, 0x4e, 0x49, 0x4c,
	/* 112 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x00, 0x48,
	/* 120 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0xe3, 0xe7, 0x65,
};

uint16_t iokitd_input_keyboard_usage(int code)
{
	if (code >= 0 && code < 128)
		return key_usage_low[code];
	if (code >= 183 && code <= 194)   /* KEY_F13 .. KEY_F24 */
		return 0x68 + (code - 183);
	return 0;
}

static int bit_is_set(const uint8_t* bits, int bit)
{
	return (bits[bit / 8] >> (bit % 8)) & 1;
}

static void copy_string(char* to, size_t size, const char* from)
{
	snprintf(to, size, "%s", from);
}

/* The letter keys, Enter and Space: what separates a keyboard from a node with a few keys */
static int has_typing_keys(const uint8_t* key_bits)
{
	static const uint8_t letters[] = {
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25,      /* Q .. P */
		30, 31, 32, 33, 34, 35, 36, 37, 38,          /* A .. L */
		44, 45, 46, 47, 48, 49, 50,                  /* Z .. M */
		KEY_ENTER, KEY_SPACE,
	};

	for (size_t i = 0; i < sizeof(letters); i++)
		if (!bit_is_set(key_bits, letters[i]))
			return 0;
	return 1;
}

int iokitd_input_probe(const char* path, struct iokitd_input* dev)
{
	uint8_t ev_bits[4] = {0}, rel_bits[REL_CNT / 8] = {0}, key_bits[KEY_CNT / 8] = {0};
	uint16_t id[4] = {0};
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return -1;

	memset(dev, 0, sizeof(*dev));
	copy_string(dev->path, sizeof(dev->path), path);

	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0
		|| ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0
		|| !bit_is_set(ev_bits, EV_KEY))
	{
		close(fd);
		return -1;
	}

	if (has_typing_keys(key_bits))
	{
		dev->kind = IOKITD_INPUT_KEYBOARD;
		dev->report_size = IOKITD_KEYBOARD_REPORT_SIZE;
	}
	else if (bit_is_set(ev_bits, EV_REL)
		&& ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) >= 0
		&& bit_is_set(rel_bits, REL_X) && bit_is_set(rel_bits, REL_Y)
		&& bit_is_set(key_bits, BTN_LEFT))
	{
		size_t offset;

		dev->kind = IOKITD_INPUT_MOUSE;
		dev->button_count = 1;
		for (int code = BTN_LEFT; code <= BTN_EXTRA; code++)
			if (bit_is_set(key_bits, code))
				dev->button_count = code - BTN_LEFT + 1;
		dev->has_wheel = bit_is_set(rel_bits, REL_WHEEL);
		dev->has_hwheel = bit_is_set(rel_bits, REL_HWHEEL);

		offset = 1;
		dev->x_offset = (uint16_t) offset;
		offset += 2;
		dev->y_offset = (uint16_t) offset;
		offset += 2;
		if (dev->has_wheel)
			dev->wheel_offset = (uint16_t) offset++;
		if (dev->has_hwheel)
			dev->hwheel_offset = (uint16_t) offset++;
		dev->report_size = (uint16_t) offset;
	}
	else
	{
		close(fd);
		return -1;
	}

	if (ioctl(fd, EVIOCGID, id) == 0)
	{
		dev->bustype = id[0];
		dev->vendor = id[1];
		dev->product = id[2];
		dev->version = id[3];
	}
	if (ioctl(fd, EVIOCGNAME(sizeof(dev->name) - 1), dev->name) < 0)
		dev->name[0] = '\0';
	if (ioctl(fd, EVIOCGUNIQ(sizeof(dev->serial) - 1), dev->serial) < 0)
		dev->serial[0] = '\0';

	close(fd);
	return 0;
}

int iokitd_input_list(const char* directory, char paths[][64], int max)
{
	return iokitd_gamepad_list(directory, paths, max);
}

int iokitd_input_open(const char* path)
{
	return iokitd_gamepad_open(path);
}

static void set_bit(uint8_t* report, int bit, int on)
{
	if (on)
		report[bit / 8] |= (uint8_t) (1u << (bit % 8));
	else
		report[bit / 8] &= (uint8_t) ~(1u << (bit % 8));
}

static int add_clamped(int32_t current, int32_t delta, int32_t minimum, int32_t maximum)
{
	int64_t sum = (int64_t) current + delta;

	if (sum < minimum)
		return minimum;
	if (sum > maximum)
		return maximum;
	return (int) sum;
}

static void add_motion16(uint8_t* report, uint16_t offset, int32_t delta)
{
	int16_t current = (int16_t) (report[offset] | (report[offset + 1] << 8));
	uint16_t value = (uint16_t) (int16_t) add_clamped(current, delta, -32768, 32767);

	report[offset] = (uint8_t) value;
	report[offset + 1] = (uint8_t) (value >> 8);
}

static void add_motion8(uint8_t* report, uint16_t offset, int32_t delta)
{
	int8_t current = (int8_t) report[offset];

	report[offset] = (uint8_t) (int8_t) add_clamped(current, delta, -127, 127);
}

int iokitd_input_apply_event(const struct iokitd_input* dev, uint8_t* report,
	uint16_t type, uint16_t code, int32_t value)
{
	uint8_t before[IOKITD_INPUT_MAX_REPORT];
	size_t size = dev->report_size;

	if (size > sizeof(before))
		return 0;
	memcpy(before, report, size);

	if (dev->kind == IOKITD_INPUT_KEYBOARD)
	{
		uint16_t usage = type == EV_KEY ? iokitd_input_keyboard_usage(code) : 0;

		/* value 2 is auto-repeat: the key is already down */
		if (usage >= 0xe0 && usage <= 0xe7 && value != 2)
			set_bit(report, usage - 0xe0, value != 0);
		else if (usage >= IOKITD_KEYBOARD_FIRST_USAGE && usage <= IOKITD_KEYBOARD_LAST_USAGE && value != 2)
			set_bit(report + 1, usage - IOKITD_KEYBOARD_FIRST_USAGE, value != 0);
	}
	else if (dev->kind == IOKITD_INPUT_MOUSE)
	{
		if (type == EV_KEY && code >= BTN_LEFT && code < BTN_LEFT + dev->button_count)
			set_bit(report, code - BTN_LEFT, value != 0);
		else if (type == EV_REL)
		{
			if (code == REL_X)
				add_motion16(report, dev->x_offset, value);
			else if (code == REL_Y)
				add_motion16(report, dev->y_offset, value);
			else if (code == REL_WHEEL && dev->has_wheel)
				add_motion8(report, dev->wheel_offset, value);
			else if (code == REL_HWHEEL && dev->has_hwheel)
				add_motion8(report, dev->hwheel_offset, value);
		}
	}

	return memcmp(before, report, size) != 0;
}

/* Forgets the motion of the finished frame */
static void clear_motion(const struct iokitd_input* dev, uint8_t* report)
{
	if (dev->kind != IOKITD_INPUT_MOUSE)
		return;
	memset(report + dev->x_offset, 0, dev->report_size - dev->x_offset);
}

void iokitd_input_initial_report(int fd, const struct iokitd_input* dev, uint8_t* report)
{
	uint8_t keys[KEY_CNT / 8] = {0};

	memset(report, 0, dev->report_size);

	if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) != 0)
		return;
	for (int code = 0; code < KEY_CNT; code++)
		if (bit_is_set(keys, code))
			iokitd_input_apply_event(dev, report, EV_KEY, code, 1);
}

int iokitd_input_read(int fd, const struct iokitd_input* dev, uint8_t* report,
	iokitd_input_frame_fn frame, void* context)
{
	struct evdev_event events[32];
	int changed = 0, dirty = 0;
	/* Applied to a copy until SYN_REPORT, so a half-delivered frame is never visible */
	uint8_t pending[IOKITD_INPUT_MAX_REPORT];

	if (dev->report_size > sizeof(pending))
		return 0;
	memcpy(pending, report, dev->report_size);
	clear_motion(dev, pending);

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
				if (e->code == SYN_REPORT && dirty)
				{
					memcpy(report, pending, dev->report_size);
					if (frame)
						frame(report, context);
					clear_motion(dev, pending);
					changed = 1;
					dirty = 0;
				}
				else if (e->code == SYN_DROPPED)
				{
					/* The kernel's queue overflowed: events were lost, so resynchronise */
					iokitd_input_initial_report(fd, dev, pending);
					dirty = memcmp(pending, report, dev->report_size) != 0;
				}
			}
			else if (iokitd_input_apply_event(dev, pending, e->type, e->code, e->value))
				dirty = 1;
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

/* Items whose value is unsigned (usages): one byte up to 0xff, two up to 0xffff */
static void put_uitem(struct descriptor_writer* w, uint8_t prefix, uint32_t value)
{
	int bytes = value <= 0xff ? 1 : 2;

	put_byte(w, (uint8_t) (prefix | bytes));
	for (int i = 0; i < bytes; i++)
		put_byte(w, (uint8_t) (value >> (8 * i)));
}

#define USAGE_PAGE(w, v) put_uitem(w, 0x04, v)
#define USAGE(w, v) put_uitem(w, 0x08, v)
#define USAGE_MIN(w, v) put_uitem(w, 0x18, v)
#define USAGE_MAX(w, v) put_uitem(w, 0x28, v)
#define LOGICAL_MIN(w, v) put_item(w, 0x14, v)
#define LOGICAL_MAX(w, v) put_item(w, 0x24, v)
#define REPORT_SIZE(w, v) put_item(w, 0x74, v)
#define REPORT_COUNT(w, v) put_item(w, 0x94, v)
#define INPUT(w, v) put_item(w, 0x80, v)
#define COLLECTION(w, v) put_item(w, 0xa0, v)

static void relative_axis(struct descriptor_writer* w, int32_t minimum, int32_t maximum, int bits, int count)
{
	LOGICAL_MIN(w, minimum);
	LOGICAL_MAX(w, maximum);
	REPORT_SIZE(w, bits);
	REPORT_COUNT(w, count);
	INPUT(w, 0x06);                 /* Data, Variable, Relative */
}

int iokitd_input_report_descriptor(const struct iokitd_input* dev, uint8_t* out, size_t size)
{
	struct descriptor_writer w = { out, size, 0, 0 };

	USAGE_PAGE(&w, 0x01);
	USAGE(&w, iokitd_input_primary_usage(dev));
	COLLECTION(&w, 0x01);           /* Application */

	if (dev->kind == IOKITD_INPUT_KEYBOARD)
	{
		USAGE_PAGE(&w, 0x07);
		USAGE_MIN(&w, 0xe0);
		USAGE_MAX(&w, 0xe7);
		LOGICAL_MIN(&w, 0);
		LOGICAL_MAX(&w, 1);
		REPORT_SIZE(&w, 1);
		REPORT_COUNT(&w, 8);
		INPUT(&w, 0x02);            /* Data, Variable, Absolute */

		USAGE_MIN(&w, IOKITD_KEYBOARD_FIRST_USAGE);
		USAGE_MAX(&w, IOKITD_KEYBOARD_LAST_USAGE);
		LOGICAL_MIN(&w, 0);
		LOGICAL_MAX(&w, 1);
		REPORT_SIZE(&w, 1);
		REPORT_COUNT(&w, IOKITD_KEYBOARD_LAST_USAGE - IOKITD_KEYBOARD_FIRST_USAGE + 1);
		INPUT(&w, 0x02);
	}
	else
	{
		USAGE(&w, 0x01);            /* Pointer */
		COLLECTION(&w, 0x00);       /* Physical */

		USAGE_PAGE(&w, 0x09);
		USAGE_MIN(&w, 1);
		USAGE_MAX(&w, dev->button_count);
		LOGICAL_MIN(&w, 0);
		LOGICAL_MAX(&w, 1);
		REPORT_SIZE(&w, 1);
		REPORT_COUNT(&w, dev->button_count);
		INPUT(&w, 0x02);
		if (dev->button_count % 8)
		{
			REPORT_SIZE(&w, 1);
			REPORT_COUNT(&w, 8 - dev->button_count % 8);
			INPUT(&w, 0x03);        /* Constant */
		}

		USAGE_PAGE(&w, 0x01);
		USAGE(&w, 0x30);
		USAGE(&w, 0x31);
		relative_axis(&w, -32768, 32767, 16, 2);
		if (dev->has_wheel)
		{
			USAGE(&w, 0x38);
			relative_axis(&w, -127, 127, 8, 1);
		}
		if (dev->has_hwheel)
		{
			USAGE_PAGE(&w, 0x0c);
			USAGE(&w, 0x238);       /* AC Pan */
			relative_axis(&w, -127, 127, 8, 1);
		}
		put_byte(&w, 0xc0);         /* End Collection (Physical) */
	}

	put_byte(&w, 0xc0);             /* End Collection (Application) */

	return w.overflow ? -1 : (int) w.used;
}

#else /* !__LP64__ */

uint16_t iokitd_input_keyboard_usage(int code) { return 0; }
int iokitd_input_probe(const char* path, struct iokitd_input* dev) { return -1; }
int iokitd_input_list(const char* directory, char paths[][64], int max) { return 0; }
int iokitd_input_open(const char* path) { return -1; }
int iokitd_input_read(int fd, const struct iokitd_input* dev, uint8_t* report, iokitd_input_frame_fn frame, void* context) { return -1; }
void iokitd_input_initial_report(int fd, const struct iokitd_input* dev, uint8_t* report) {}
int iokitd_input_apply_event(const struct iokitd_input* dev, uint8_t* report, uint16_t type, uint16_t code, int32_t value) { return 0; }
int iokitd_input_report_descriptor(const struct iokitd_input* dev, uint8_t* out, size_t size) { return -1; }

#endif
