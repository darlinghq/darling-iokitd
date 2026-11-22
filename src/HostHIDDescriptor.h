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
 * A parser for HID report descriptors (HID 1.11, section 6.2.2), producing
 * the elements an IOHIDDevice publishes for the descriptor: what a client of
 * IOHIDDeviceCopyMatchingElements() sees, with cookies and positions of the
 * values in the reports. It has no Darling dependencies and builds natively
 * for testing.
 *
 * The rules for the elements follow the public contract of IOHIDElement and
 * what IOHIDManager shows for devices on macOS (black-box observation of a
 * keyboard and a tablet on macOS 10.14, recorded in the tests):
 *  - Collections are elements. Cookies run from 1.
 *  - Every field of a Data variable item is an element with its own cookie
 *    and a report count of 1. Fields whose usages are consecutive (a usage
 *    range) are one entry with a range of cookies, the same usage repeated
 *    for several fields (one usage and a count above 1) is one entry whose
 *    fields share the usage (the first element then stands for all of them).
 *    Constant items have no elements.
 *  - A Data array item has one element per slot (usage 0xFFFFFFFF, the
 *    report count in the first), followed after the report handler by one
 *    1-bit element per usage of the declared range, whose value is 1 while
 *    the usage is in a slot.
 *  - Cookies are assigned to collections and inputs in descriptor order, then
 *    to the report handlers (one per input report), the usage elements of
 *    arrays, then the outputs (and their usage elements), then the features.
 *  - A physical range of 0..0 is the logical range. A logical maximum of
 *    a variable item that does not fit in the signed item and follows a
 *    non-negative minimum is unsigned.
 */

#ifndef IOKITD_HOST_HID_DESCRIPTOR_H
#define IOKITD_HOST_HID_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum iokitd_hid_kind
{
	IOKITD_HID_INPUT = 0,
	IOKITD_HID_OUTPUT = 1,
	IOKITD_HID_FEATURE = 2,
	IOKITD_HID_COLLECTION = 3,
	IOKITD_HID_HANDLER = 4,      /* the input report as a whole, for the report with report_id */
};

enum iokitd_hid_array
{
	IOKITD_HID_VARIABLE = 0,
	IOKITD_HID_ARRAY_SLOTS = 1,  /* the slots of an array item */
	IOKITD_HID_ARRAY_USAGES = 2, /* an element per usage of an array item's range */
};

/* Flags of a main item (the item's data) */
#define IOKITD_HID_FLAG_CONSTANT 0x01
#define IOKITD_HID_FLAG_VARIABLE 0x02
#define IOKITD_HID_FLAG_RELATIVE 0x04
#define IOKITD_HID_FLAG_WRAP 0x08
#define IOKITD_HID_FLAG_NONLINEAR 0x10
#define IOKITD_HID_FLAG_NO_PREFERRED 0x20
#define IOKITD_HID_FLAG_NULL_STATE 0x40
#define IOKITD_HID_FLAG_VOLATILE 0x80

/*
 * One entry of the element list: the elements with the cookies cookie_min
 * .. cookie_max, which have the usages usage_min .. usage_max in turn when
 * those differ (and the same usage when they are equal).
 */
struct iokitd_hid_element
{
	int kind;                    /* enum iokitd_hid_kind */
	int array;                   /* enum iokitd_hid_array */
	int duplicates;              /* the fields of the cookie range share one usage */
	uint32_t cookie_min, cookie_max;
	uint32_t parent;             /* cookie of the collection, 0 for none */
	uint32_t flags;
	uint32_t collection_type;
	uint32_t usage_page, usage_min, usage_max;
	int32_t logical_min, logical_max, physical_min, physical_max;
	uint32_t unit, unit_exponent;
	uint32_t report_id;
	uint32_t size;               /* bits of the first element's value (all the fields for duplicates) */
	uint32_t report_size;        /* bits of one field */
	uint32_t report_count;       /* the count of the item for the first element of duplicates and slots */
	uint32_t bit_offset;         /* of the first field in the report, without the report ID byte */
	uint32_t app_page, app_usage;/* of the top-level collection the element is in */
	/* ARRAY_USAGES: the slots it reads */
	uint32_t slot_bit_offset, slot_size, slot_count;
	uint32_t field_count;        /* fields in the cookie range of a variable entry (cookies for slots and usages differ) */
};

/* The size of a report, without its report ID byte */
struct iokitd_hid_report
{
	int kind;                    /* IOKITD_HID_INPUT, OUTPUT or FEATURE */
	uint32_t id;
	uint32_t bits;
};

struct iokitd_hid_usage_pair
{
	uint32_t page, usage;
};

struct iokitd_hid_descriptor
{
	struct iokitd_hid_element* elements;
	size_t element_count;
	struct iokitd_hid_report* reports;
	size_t report_count;
	struct iokitd_hid_usage_pair* pairs;   /* of the top-level collections, without repeats */
	size_t pair_count;
	uint32_t last_cookie;
};

/* The most usages an array item's range stands for */
#define IOKITD_HID_MAX_ARRAY_USAGES 1024

/*
 * Parses a descriptor. Returns 0 and fills *out (free it with
 * iokitd_hid_free), or -1 for a descriptor that is malformed (truncated
 * item, unbalanced collections, a main item that exceeds the size limits) or
 * has no input, output or feature item. Items it does not know are skipped.
 */
int iokitd_hid_parse(const uint8_t* data, size_t size, struct iokitd_hid_descriptor* out);
void iokitd_hid_free(struct iokitd_hid_descriptor* descriptor);

/* The length in bytes of the report of the kind and ID, without the ID byte; 0 if there is none */
uint32_t iokitd_hid_report_bytes(const struct iokitd_hid_descriptor* descriptor, int kind, uint32_t id);

/* The longest report of the kind in bytes, with its ID byte when it has an ID */
uint32_t iokitd_hid_max_report_bytes(const struct iokitd_hid_descriptor* descriptor, int kind);

#ifdef __cplusplus
}
#endif

#endif
