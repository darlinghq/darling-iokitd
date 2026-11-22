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

#include "HostHIDDevice.h"
#include "HostInput.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

#define ABS_X 0x00
#define ABS_RZ 0x05
#define ABS_THROTTLE 0x06
#define ABS_RUDDER 0x07
#define ABS_WHEEL 0x08
#define ABS_GAS 0x09
#define ABS_BRAKE 0x0a
#define ABS_HAT0X 0x10
#define ABS_HAT3Y 0x17

#define REL_X 0x00
#define REL_RZ 0x05
#define REL_HWHEEL 0x06
#define REL_DIAL 0x07
#define REL_WHEEL 0x08

#define BTN_MISC 0x100
#define BTN_MOUSE 0x110
#define BTN_JOYSTICK 0x120
#define BTN_GAMEPAD 0x130
#define BTN_DPAD_UP 0x220
#define BTN_TRIGGER_HAPPY 0x2c0
#define KEY_MAX_CODE 0x2ff

#define IOC_READ_DIR 2u
#define IOC_ENCODE(dir, type, nr, size) \
	(((unsigned long)(dir) << 30) | ((unsigned long)(size) << 16) | ((unsigned long)(type) << 8) | (unsigned long)(nr))
#define EVIOCGKEY(len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x18, len)
#define EVIOCGBIT(ev, len) IOC_ENCODE(IOC_READ_DIR, 'E', 0x20 + (ev), len)
#define EVIOCGABS(abs) IOC_ENCODE(IOC_READ_DIR, 'E', 0x40 + (abs), 24)

/* Generic Desktop and other usages */
#define PAGE_GENERIC_DESKTOP 0x01
#define PAGE_SIMULATION 0x02
#define PAGE_KEYBOARD 0x07
#define PAGE_LED 0x08
#define PAGE_BUTTON 0x09
#define PAGE_CONSUMER 0x0c

enum source_kind
{
	SOURCE_NONE,
	SOURCE_KEY,
	SOURCE_ABS,
	SOURCE_REL,
	SOURCE_HAT,
};

struct field
{
	size_t report;          /* index into the input reports */
	uint32_t bit_offset, bits;
	int32_t logical_min, logical_max;
	int source;
	uint16_t code;          /* the event code; for a hat the code of its X axis (Y follows) */
};

struct usage_range
{
	uint32_t page, min, max;
};

/* An array item: its slots and the usages of its range */
struct slots
{
	size_t report;
	uint32_t bit_offset, slot_bits, slot_count;
	struct usage_range ranges[8];
	size_t range_count;
};

struct input_report
{
	uint32_t id;
	uint32_t bytes;
	uint8_t* data;
	uint8_t* scratch;
};

struct iokitd_hid_device
{
	struct iokitd_hid_descriptor descriptor;
	struct iokitd_hid_caps caps;
	struct input_report* inputs;
	size_t input_count;
	struct field* fields;
	size_t field_count, field_capacity;
	struct slots* slots;
	size_t slot_group_count, slot_group_capacity;

	uint8_t keys[IOKITD_HID_KEY_BYTES];
	int32_t abs[IOKITD_HID_ABS_COUNT];
	int32_t rel[IOKITD_HID_REL_COUNT];
	int rel_stale;
	int dirty;
	int modifier_fields;   /* the modifier usages (0xe0 .. 0xe7) are fields of their own */
};

static int bit_is_set(const uint8_t* bits, size_t size, unsigned bit)
{
	return bit / 8 < size && ((bits[bit / 8] >> (bit % 8)) & 1);
}

int iokitd_hid_caps_read(int fd, struct iokitd_hid_caps* caps)
{
	uint8_t ev_bits[4] = {0};

	memset(caps, 0, sizeof(*caps));
	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
		return -1;
	if (bit_is_set(ev_bits, sizeof(ev_bits), EV_KEY))
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(caps->keys)), caps->keys);
	if (bit_is_set(ev_bits, sizeof(ev_bits), EV_ABS))
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(caps->abs)), caps->abs);
	if (bit_is_set(ev_bits, sizeof(ev_bits), EV_REL))
		ioctl(fd, EVIOCGBIT(EV_REL, sizeof(caps->rel)), caps->rel);
	return 0;
}

int iokitd_hid_key_code(uint32_t usage)
{
	static int16_t reverse[256];
	static int built;

	if (!built)
	{
		for (int code = 255; code >= 0; code--)
		{
			uint16_t u = iokitd_input_keyboard_usage(code);
			if (u)
				reverse[u] = (int16_t) code;
		}
		built = 1;
	}
	return usage < 256 ? reverse[usage] : 0;
}

/* The event code the kernel's HID input layer makes for a usage; 0 for no source */
static int source_of(uint32_t page, uint32_t usage, uint32_t app_page, uint32_t app_usage, uint32_t flags,
	const struct iokitd_hid_caps* caps, int* hat_count, uint16_t* code)
{
	int relative = (flags & IOKITD_HID_FLAG_RELATIVE) != 0;

	if (page == PAGE_BUTTON && usage >= 1)
	{
		unsigned k = usage - 1, base;

		if (app_page == PAGE_GENERIC_DESKTOP && app_usage == 4)
			base = BTN_JOYSTICK;
		else if (app_page == PAGE_GENERIC_DESKTOP && app_usage == 5)
			base = BTN_GAMEPAD;
		else if (app_page == PAGE_GENERIC_DESKTOP && (app_usage == 2 || app_usage == 1))
			base = BTN_MOUSE;
		else
			base = BTN_MISC;
		unsigned c = k <= 0xf ? base + k : BTN_TRIGGER_HAPPY + (k - 0x10);
		if (c <= KEY_MAX_CODE && bit_is_set(caps->keys, sizeof(caps->keys), c))
		{
			*code = (uint16_t) c;
			return SOURCE_KEY;
		}
		return SOURCE_NONE;
	}
	if (page == PAGE_KEYBOARD)
	{
		int c = iokitd_hid_key_code(usage);

		if (c && bit_is_set(caps->keys, sizeof(caps->keys), c))
		{
			*code = (uint16_t) c;
			return SOURCE_KEY;
		}
		return SOURCE_NONE;
	}
	if (page == PAGE_GENERIC_DESKTOP)
	{
		if (usage >= 0x30 && usage <= 0x35)
		{
			unsigned c = usage - 0x30;

			if (relative)
			{
				if (bit_is_set(caps->rel, sizeof(caps->rel), REL_X + c))
				{
					*code = (uint16_t) (REL_X + c);
					return SOURCE_REL;
				}
			}
			else if (bit_is_set(caps->abs, sizeof(caps->abs), ABS_X + c))
			{
				*code = (uint16_t) (ABS_X + c);
				return SOURCE_ABS;
			}
			return SOURCE_NONE;
		}
		if (usage == 0x36 || usage == 0x37 || usage == 0x38)
		{
			/* Slider, Dial, Wheel */
			unsigned rel = usage == 0x38 ? REL_WHEEL : usage == 0x37 ? REL_DIAL : 0xffff;
			unsigned abs = usage == 0x38 ? ABS_WHEEL : usage == 0x36 ? ABS_THROTTLE : ABS_RUDDER;

			if (relative && rel != 0xffff && bit_is_set(caps->rel, sizeof(caps->rel), rel))
			{
				*code = (uint16_t) rel;
				return SOURCE_REL;
			}
			if (!relative && bit_is_set(caps->abs, sizeof(caps->abs), abs))
			{
				*code = (uint16_t) abs;
				return SOURCE_ABS;
			}
			return SOURCE_NONE;
		}
		if (usage == 0x39)
		{
			unsigned x = ABS_HAT0X + 2 * (unsigned) *hat_count;

			if (x + 1 <= ABS_HAT3Y && bit_is_set(caps->abs, sizeof(caps->abs), x) && bit_is_set(caps->abs, sizeof(caps->abs), x + 1))
			{
				*code = (uint16_t) x;
				(*hat_count)++;
				return SOURCE_HAT;
			}
			(*hat_count)++;
			return SOURCE_NONE;
		}
		if (usage >= 0x90 && usage <= 0x93)
		{
			/* Up, Down, Right, Left of a directional pad: BTN_DPAD_UP, _DOWN, _LEFT, _RIGHT */
			static const uint16_t dpad[4] = { BTN_DPAD_UP, BTN_DPAD_UP + 1, BTN_DPAD_UP + 3, BTN_DPAD_UP + 2 };
			unsigned c = dpad[usage - 0x90];

			if (bit_is_set(caps->keys, sizeof(caps->keys), c))
			{
				*code = (uint16_t) c;
				return SOURCE_KEY;
			}
		}
		return SOURCE_NONE;
	}
	if (page == PAGE_SIMULATION)
	{
		unsigned abs;

		switch (usage)
		{
			case 0xbb: abs = ABS_THROTTLE; break;
			case 0xba: abs = ABS_RUDDER; break;
			case 0xc4: abs = ABS_GAS; break;
			case 0xc5: abs = ABS_BRAKE; break;
			case 0xc8: abs = ABS_WHEEL; break;
			default: return SOURCE_NONE;
		}
		if (!relative && bit_is_set(caps->abs, sizeof(caps->abs), abs))
		{
			*code = (uint16_t) abs;
			return SOURCE_ABS;
		}
		return SOURCE_NONE;
	}
	if (page == PAGE_CONSUMER && usage == 0x238)
	{
		/* AC Pan, the horizontal wheel */
		if (relative && bit_is_set(caps->rel, sizeof(caps->rel), REL_HWHEEL))
		{
			*code = REL_HWHEEL;
			return SOURCE_REL;
		}
	}
	return SOURCE_NONE;
}

static struct field* add_field(struct iokitd_hid_device* d)
{
	if (d->field_count == d->field_capacity)
	{
		size_t capacity = d->field_capacity ? d->field_capacity * 2 : 64;
		void* grown = realloc(d->fields, capacity * sizeof(struct field));

		if (!grown)
			return NULL;
		d->fields = grown;
		d->field_capacity = capacity;
	}
	memset(&d->fields[d->field_count], 0, sizeof(struct field));
	return &d->fields[d->field_count++];
}

static struct slots* slots_for(struct iokitd_hid_device* d, size_t report, uint32_t bit_offset)
{
	for (size_t i = 0; i < d->slot_group_count; i++)
		if (d->slots[i].report == report && d->slots[i].bit_offset == bit_offset)
			return &d->slots[i];
	if (d->slot_group_count == d->slot_group_capacity)
	{
		size_t capacity = d->slot_group_capacity ? d->slot_group_capacity * 2 : 4;
		void* grown = realloc(d->slots, capacity * sizeof(struct slots));

		if (!grown)
			return NULL;
		d->slots = grown;
		d->slot_group_capacity = capacity;
	}
	struct slots* slots = &d->slots[d->slot_group_count++];
	memset(slots, 0, sizeof(*slots));
	slots->report = report;
	slots->bit_offset = bit_offset;
	return slots;
}

static size_t input_index(const struct iokitd_hid_device* d, uint32_t id)
{
	for (size_t i = 0; i < d->input_count; i++)
		if (d->inputs[i].id == id)
			return i;
	return (size_t) -1;
}

static void put_bits(uint8_t* data, size_t bytes, uint32_t bit_offset, uint32_t bits, uint32_t value)
{
	for (uint32_t i = 0; i < bits && i < 32; i++)
	{
		uint32_t bit = bit_offset + i;

		if (bit / 8 >= bytes)
			break;
		if ((value >> i) & 1)
			data[bit / 8] |= (uint8_t) (1u << (bit % 8));
		else
			data[bit / 8] &= (uint8_t) ~(1u << (bit % 8));
	}
}

static int32_t clamp(int64_t value, int32_t minimum, int32_t maximum)
{
	if (maximum < minimum)
		return (int32_t) value;
	return (int32_t) (value < minimum ? minimum : value > maximum ? maximum : value);
}

/* Directions of a hat, up first and clockwise, for (x, y) with y negative upwards; -1 for the centre */
static int hat_direction(int x, int y)
{
	static const int8_t table[3][3] = {
		/* y = -1 */ { 7, 0, 1 },
		/* y =  0 */ { 6, -1, 2 },
		/* y = +1 */ { 5, 4, 3 },
	};
	x = x < 0 ? -1 : x > 0;
	y = y < 0 ? -1 : y > 0;
	return table[y + 1][x + 1];
}

static uint32_t hat_value(const struct field* f, int direction)
{
	int64_t span = (int64_t) f->logical_max - f->logical_min + 1;
	int64_t value;

	if (direction >= 0)
		value = f->logical_min + (direction * span) / 8;
	else if (f->logical_min > 0)
		value = f->logical_min - 1;
	else
		value = (int64_t) f->logical_max + 1;
	return (uint32_t) value;
}

static void build(struct iokitd_hid_device* d, size_t index, uint8_t* out)
{
	struct input_report* report = &d->inputs[index];

	memset(out, 0, report->bytes);
	for (size_t i = 0; i < d->field_count; i++)
	{
		const struct field* f = &d->fields[i];
		int64_t value = 0;

		if (f->report != index)
			continue;
		switch (f->source)
		{
			case SOURCE_KEY:
				value = bit_is_set(d->keys, sizeof(d->keys), f->code) ? clamp(1, f->logical_min, f->logical_max) : clamp(0, f->logical_min, f->logical_max);
				break;
			case SOURCE_ABS:
				value = clamp(d->abs[f->code], f->logical_min, f->logical_max);
				break;
			case SOURCE_REL:
				value = clamp(d->rel[f->code], f->logical_min, f->logical_max);
				break;
			case SOURCE_HAT:
				value = hat_value(f, hat_direction(d->abs[f->code], d->abs[f->code + 1]));
				break;
			default:
				value = f->logical_min > 0 ? f->logical_min : 0;
				if (f->logical_min == 0 && f->logical_max == 0)
					value = 0;
		}
		put_bits(out, report->bytes, f->bit_offset, f->bits, (uint32_t) value);
	}

	for (size_t g = 0; g < d->slot_group_count; g++)
	{
		const struct slots* s = &d->slots[g];
		uint32_t used = 0, pressed = 0;
		uint32_t usages[64];

		if (s->report != index)
			continue;
		for (size_t r = 0; r < s->range_count; r++)
		{
			if (s->ranges[r].page != PAGE_KEYBOARD)
				continue;
			for (uint32_t u = s->ranges[r].min; u <= s->ranges[r].max && u < 256; u++)
			{
				int code = iokitd_hid_key_code(u);

				/* A keyboard that has the modifiers as fields of their own does not repeat them in the array */
				if (d->modifier_fields && u >= 0xe0 && u <= 0xe7)
					continue;
				if (code && bit_is_set(d->keys, sizeof(d->keys), code) && bit_is_set(d->caps.keys, sizeof(d->caps.keys), code))
				{
					if (pressed < 64)
						usages[pressed] = u;
					pressed++;
				}
			}
		}
		/* More keys down than slots: every slot reads ErrorRollOver, as keyboards report it */
		for (uint32_t slot = 0; slot < s->slot_count; slot++)
		{
			uint32_t usage = pressed > s->slot_count ? 1 : slot < pressed ? usages[slot] : 0;
			put_bits(out, report->bytes, s->bit_offset + slot * s->slot_bits, s->slot_bits, usage);
			used++;
		}
	}
}

struct iokitd_hid_device* iokitd_hid_device_create(const uint8_t* descriptor, size_t size, const struct iokitd_hid_caps* caps)
{
	struct iokitd_hid_device* d = calloc(1, sizeof(*d));
	int hat_count = 0, mapped = 0;

	if (!d)
		return NULL;
	d->caps = *caps;
	if (iokitd_hid_parse(descriptor, size, &d->descriptor) < 0)
	{
		free(d);
		return NULL;
	}

	for (size_t i = 0; i < d->descriptor.report_count; i++)
		if (d->descriptor.reports[i].kind == IOKITD_HID_INPUT)
			d->input_count++;
	d->inputs = calloc(d->input_count ? d->input_count : 1, sizeof(struct input_report));
	if (!d->inputs)
		goto fail;
	for (size_t i = 0, n = 0; i < d->descriptor.report_count; i++)
	{
		const struct iokitd_hid_report* r = &d->descriptor.reports[i];

		if (r->kind != IOKITD_HID_INPUT)
			continue;
		d->inputs[n].id = r->id;
		d->inputs[n].bytes = (r->bits + 7) / 8;
		d->inputs[n].data = calloc(d->inputs[n].bytes ? d->inputs[n].bytes : 1, 1);
		d->inputs[n].scratch = calloc(d->inputs[n].bytes ? d->inputs[n].bytes : 1, 1);
		if (!d->inputs[n].data || !d->inputs[n].scratch)
			goto fail;
		n++;
	}

	for (size_t i = 0; i < d->descriptor.element_count; i++)
	{
		const struct iokitd_hid_element* e = &d->descriptor.elements[i];
		size_t report;

		if (e->kind != IOKITD_HID_INPUT)
			continue;
		report = input_index(d, e->report_id);
		if (report == (size_t) -1)
			continue;

		if (e->array == IOKITD_HID_ARRAY_USAGES)
		{
			struct slots* s = slots_for(d, report, e->slot_bit_offset);

			if (!s)
				goto fail;
			s->slot_bits = e->slot_size;
			s->slot_count = e->slot_count;
			if (s->range_count < sizeof(s->ranges) / sizeof(s->ranges[0]))
			{
				s->ranges[s->range_count].page = e->usage_page;
				s->ranges[s->range_count].min = e->usage_min;
				s->ranges[s->range_count].max = e->usage_max;
				s->range_count++;
			}
			continue;
		}
		if (e->array == IOKITD_HID_ARRAY_SLOTS)
		{
			if (!slots_for(d, report, e->bit_offset))
				goto fail;
			continue;
		}

		for (uint32_t k = 0; k < e->field_count; k++)
		{
			uint32_t usage = e->usage_min != e->usage_max ? e->usage_min + k : e->usage_min;
			struct field* f = add_field(d);

			if (!f)
				goto fail;
			f->report = report;
			f->bit_offset = e->bit_offset + k * e->report_size;
			f->bits = e->report_size;
			f->logical_min = e->logical_min;
			f->logical_max = e->logical_max;
			f->source = source_of(e->usage_page, usage, e->app_page, e->app_usage, e->flags, caps, &hat_count, &f->code);
			if (f->source != SOURCE_NONE)
				mapped++;
			if (f->source == SOURCE_KEY && e->usage_page == PAGE_KEYBOARD && usage >= 0xe0 && usage <= 0xe7)
				d->modifier_fields = 1;
		}
	}
	/* Array slots of devices that have no keyboard keys at all are not a source */
	for (size_t g = 0; g < d->slot_group_count; g++)
		for (size_t r = 0; r < d->slots[g].range_count; r++)
			if (d->slots[g].ranges[r].page == PAGE_KEYBOARD && d->slots[g].slot_bits)
				mapped++;
	if (!mapped)
		goto fail;

	for (size_t i = 0; i < d->input_count; i++)
		build(d, i, d->inputs[i].data);
	return d;

fail:
	iokitd_hid_device_free(d);
	return NULL;
}

void iokitd_hid_device_free(struct iokitd_hid_device* d)
{
	if (!d)
		return;
	for (size_t i = 0; i < d->input_count && d->inputs; i++)
	{
		free(d->inputs[i].data);
		free(d->inputs[i].scratch);
	}
	free(d->inputs);
	free(d->fields);
	free(d->slots);
	iokitd_hid_free(&d->descriptor);
	free(d);
}

const struct iokitd_hid_descriptor* iokitd_hid_device_descriptor(const struct iokitd_hid_device* d)
{
	return &d->descriptor;
}

size_t iokitd_hid_device_input_count(const struct iokitd_hid_device* d)
{
	return d->input_count;
}

uint32_t iokitd_hid_device_input_id(const struct iokitd_hid_device* d, size_t index)
{
	return d->inputs[index].id;
}

const uint8_t* iokitd_hid_device_input(const struct iokitd_hid_device* d, size_t index, size_t* bytes)
{
	if (bytes)
		*bytes = d->inputs[index].bytes;
	return d->inputs[index].data;
}

int iokitd_hid_device_apply(struct iokitd_hid_device* d, uint16_t type, uint16_t code, int32_t value)
{
	int changed = 0;

	if (type == EV_SYN)
		return 0;
	if (d->rel_stale)
	{
		memset(d->rel, 0, sizeof(d->rel));
		d->rel_stale = 0;
	}
	if (type == EV_KEY && code <= KEY_MAX_CODE && value != 2) /* 2: auto-repeat, the key is already down */
	{
		int was = bit_is_set(d->keys, sizeof(d->keys), code);

		if (was != (value != 0))
		{
			if (value)
				d->keys[code / 8] |= (uint8_t) (1u << (code % 8));
			else
				d->keys[code / 8] &= (uint8_t) ~(1u << (code % 8));
			changed = 1;
		}
	}
	else if (type == EV_ABS && code < IOKITD_HID_ABS_COUNT)
	{
		changed = d->abs[code] != value;
		d->abs[code] = value;
	}
	else if (type == EV_REL && code < IOKITD_HID_REL_COUNT)
	{
		d->rel[code] += value;
		changed = value != 0;
	}
	if (changed)
		d->dirty = 1;
	return changed;
}

int iokitd_hid_device_commit(struct iokitd_hid_device* d, iokitd_hid_report_fn fn, void* context)
{
	int changed = 0;

	if (!d->dirty)
		return 0;
	d->dirty = 0;
	for (size_t i = 0; i < d->input_count; i++)
	{
		struct input_report* report = &d->inputs[i];

		build(d, i, report->scratch);
		if (memcmp(report->scratch, report->data, report->bytes) == 0)
			continue;
		changed++;
		if (fn)
			fn(i, report->id, report->data, report->scratch, report->bytes, context);
		memcpy(report->data, report->scratch, report->bytes);
	}
	d->rel_stale = 1;
	return changed;
}

/* The held keys and axis positions of the node */
static void load_state(struct iokitd_hid_device* d, int fd)
{
	uint8_t keys[IOKITD_HID_KEY_BYTES] = {0};

	if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) == 0)
		memcpy(d->keys, keys, sizeof(keys));
	for (size_t i = 0; i < d->field_count; i++)
	{
		const struct field* f = &d->fields[i];
		unsigned first = f->code, count = f->source == SOURCE_HAT ? 2 : f->source == SOURCE_ABS ? 1 : 0;

		for (unsigned c = first; c < first + count; c++)
		{
			int32_t info[6] = {0};

			if (ioctl(fd, EVIOCGABS(c), info) == 0)
				d->abs[c] = info[0];
		}
	}
}

void iokitd_hid_device_initial(struct iokitd_hid_device* d, int fd)
{
	load_state(d, fd);
	for (size_t i = 0; i < d->input_count; i++)
		build(d, i, d->inputs[i].data);
	d->dirty = 0;
}

#if defined(__LP64__)

struct evdev_event
{
	int64_t seconds, microseconds;
	uint16_t type, code;
	int32_t value;
};

int iokitd_hid_device_read(struct iokitd_hid_device* d, int fd, iokitd_hid_report_fn fn, void* context)
{
	struct evdev_event events[32];
	int changed = 0;

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

			if (e->type == EV_SYN && e->code == SYN_REPORT)
				changed += iokitd_hid_device_commit(d, fn, context);
			else if (e->type == EV_SYN && e->code == SYN_DROPPED)
			{
				/* The kernel's queue overflowed: events were lost, so resynchronise */
				load_state(d, fd);
				d->dirty = 1;
			}
			else
				iokitd_hid_device_apply(d, e->type, e->code, e->value);
		}
	}
	return changed != 0;
}

#else

int iokitd_hid_device_read(struct iokitd_hid_device* d, int fd, iokitd_hid_report_fn fn, void* context)
{
	return -1;
}

#endif

int iokitd_hid_output_leds(const struct iokitd_hid_descriptor* descriptor, uint32_t id, const uint8_t* data, size_t length, uint8_t* leds)
{
	int found = 0;

	*leds = 0;
	for (size_t i = 0; i < descriptor->element_count; i++)
	{
		const struct iokitd_hid_element* e = &descriptor->elements[i];

		if (e->kind != IOKITD_HID_OUTPUT || e->array != IOKITD_HID_VARIABLE || e->report_id != id || e->usage_page != PAGE_LED)
			continue;
		for (uint32_t k = 0; k < e->field_count; k++)
		{
			uint32_t usage = e->usage_min != e->usage_max ? e->usage_min + k : e->usage_min;
			uint32_t bit = e->bit_offset + k * e->report_size, value = 0;

			if (usage < 1 || usage > IOKITD_KEYBOARD_LED_COUNT)
				continue;
			found = 1;
			for (uint32_t b = 0; b < e->report_size && b < 32; b++)
				if (bit + b < length * 8 && ((data[(bit + b) / 8] >> ((bit + b) % 8)) & 1))
					value |= 1u << b;
			if (value)
				*leds |= (uint8_t) (1u << (usage - 1));
		}
	}
	return found;
}

static int read_file(const char* path, uint8_t** data, size_t* size)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	uint8_t* buffer;
	ssize_t n;

	if (fd < 0)
		return -1;
	buffer = malloc(4096 + 1);
	if (!buffer)
	{
		close(fd);
		return -1;
	}
	n = read(fd, buffer, 4096);
	close(fd);
	if (n <= 0)
	{
		free(buffer);
		return -1;
	}
	*data = buffer;
	*size = (size_t) n;
	return 0;
}

static int is_event_name(const char* name)
{
	if (strncmp(name, "event", 5) != 0 || !name[5])
		return 0;
	for (const char* c = name + 5; *c; c++)
		if (*c < '0' || *c > '9')
			return 0;
	return strlen(name) < 32;
}

int iokitd_hid_sysfs_descriptor(const char* sysroot, const char* event_name, uint8_t** descriptor, size_t* size,
	char* hid_dir, size_t hid_dir_size)
{
	char link[PATH_MAX], real[PATH_MAX], file[PATH_MAX + 32];
	size_t floor_length;

	if (!is_event_name(event_name))
		return -1;
	snprintf(link, sizeof(link), "%s/class/input/%s", sysroot, event_name);
	if (!realpath(link, real))
		return -1;

	/* The HID device is an ancestor of the node: .../<hid device>/input/input<N>/event<M> */
	snprintf(file, sizeof(file), "%s/devices", sysroot);
	floor_length = strlen(file);
	for (int level = 0; level < 8; level++)
	{
		char* slash = strrchr(real, '/');

		if (!slash || (size_t) (slash - real) <= floor_length)
			return -1;
		*slash = '\0';
		snprintf(file, sizeof(file), "%s/report_descriptor", real);
		if (read_file(file, descriptor, size) == 0)
		{
			snprintf(hid_dir, hid_dir_size, "%s", real);
			return 0;
		}
	}
	return -1;
}

int iokitd_hid_sysfs_siblings(const char* hid_dir, char names[][32], int max)
{
	char path[PATH_MAX];
	DIR* inputs;
	struct dirent* entry;
	int count = 0;

	snprintf(path, sizeof(path), "%s/input", hid_dir);
	inputs = opendir(path);
	if (!inputs)
		return 0;
	while ((entry = readdir(inputs)) != NULL && count < max)
	{
		DIR* nodes;
		struct dirent* node;

		if (strncmp(entry->d_name, "input", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "%s/input/%s", hid_dir, entry->d_name);
		nodes = opendir(path);
		if (!nodes)
			continue;
		while ((node = readdir(nodes)) != NULL && count < max)
			if (is_event_name(node->d_name))
				snprintf(names[count++], 32, "%.31s", node->d_name);
		closedir(nodes);
	}
	closedir(inputs);
	return count;
}
