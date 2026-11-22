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

#include "HostHIDDescriptor.h"

#include <stdlib.h>
#include <string.h>

#define MAX_COLLECTION_DEPTH 32
#define MAX_USAGES 4096
#define MAX_FIELDS 65536
#define MAX_REPORT_BITS (1u << 24)
#define MAX_ENTRIES 100000

/* Where in the cookie order an entry goes (see the header) */
enum
{
	PASS_INPUT = 0,
	PASS_HANDLER = 1,
	PASS_INPUT_USAGES = 2,
	PASS_OUTPUT = 3,
	PASS_OUTPUT_USAGES = 4,
	PASS_FEATURE = 5,
	PASS_FEATURE_USAGES = 6,
};

struct entry
{
	struct iokitd_hid_element e;
	int pass;
	size_t seq;
	long parent_seq;        /* seq of the collection entry, -1 for none */
	uint32_t cookies;       /* how many cookies the entry takes */
};

struct usage_item
{
	uint32_t usage;
	int extended;           /* the item carried its own usage page in the upper half */
};

struct globals
{
	uint32_t page;
	int32_t logical_min, logical_max, physical_min, physical_max;
	uint32_t logical_max_raw;   /* the maximum as the unsigned value of its item */
	uint32_t unit, unit_exponent;
	uint32_t report_size, report_id, report_count;
};

struct locals
{
	struct usage_item usages[MAX_USAGES];
	size_t usage_count;
	int has_min, has_max;
	struct usage_item min, max;
};

struct parser
{
	struct locals locals;
	uint32_t pages[MAX_USAGES], usages[MAX_USAGES];   /* scratch of data_item() */
	struct entry* entries;
	size_t entry_count, entry_capacity;
	struct iokitd_hid_report* reports;
	size_t report_count, report_capacity;
	struct iokitd_hid_usage_pair* pairs;
	size_t pair_count, pair_capacity;
	struct globals global, stack[MAX_COLLECTION_DEPTH];
	int global_depth;
	size_t open[MAX_COLLECTION_DEPTH];  /* seq of the open collections */
	uint32_t open_app_page[MAX_COLLECTION_DEPTH], open_app_usage[MAX_COLLECTION_DEPTH];
	int depth;
	size_t next_seq;
	int failed;
};

static int grow(void** array, size_t* capacity, size_t count, size_t size)
{
	if (count < *capacity)
		return 0;
	size_t capacity_new = *capacity ? *capacity * 2 : 32;
	void* grown = realloc(*array, capacity_new * size);
	if (!grown)
		return -1;
	*array = grown;
	*capacity = capacity_new;
	return 0;
}

static struct entry* add_entry(struct parser* p)
{
	if (p->entry_count >= MAX_ENTRIES || grow((void**) &p->entries, &p->entry_capacity, p->entry_count, sizeof(struct entry)) < 0)
	{
		p->failed = 1;
		return NULL;
	}
	struct entry* entry = &p->entries[p->entry_count++];
	memset(entry, 0, sizeof(*entry));
	entry->seq = p->next_seq++;
	entry->parent_seq = p->depth ? (long) p->open[p->depth - 1] : -1;
	entry->cookies = 1;
	if (p->depth)
	{
		entry->e.app_page = p->open_app_page[0];
		entry->e.app_usage = p->open_app_usage[0];
	}
	return entry;
}

/* The bits of the report of the kind and ID, which grows by bits */
static uint32_t take_bits(struct parser* p, int kind, uint32_t id, uint32_t bits)
{
	for (size_t i = 0; i < p->report_count; i++)
	{
		if (p->reports[i].kind == kind && p->reports[i].id == id)
		{
			uint32_t first = p->reports[i].bits;
			p->reports[i].bits += bits;
			return first;
		}
	}
	if (grow((void**) &p->reports, &p->report_capacity, p->report_count, sizeof(*p->reports)) < 0)
	{
		p->failed = 1;
		return 0;
	}
	p->reports[p->report_count].kind = kind;
	p->reports[p->report_count].id = id;
	p->reports[p->report_count].bits = bits;
	p->report_count++;
	return 0;
}

static void add_pair(struct parser* p, uint32_t page, uint32_t usage)
{
	for (size_t i = 0; i < p->pair_count; i++)
		if (p->pairs[i].page == page && p->pairs[i].usage == usage)
			return;
	if (grow((void**) &p->pairs, &p->pair_capacity, p->pair_count, sizeof(*p->pairs)) < 0)
	{
		p->failed = 1;
		return;
	}
	p->pairs[p->pair_count].page = page;
	p->pairs[p->pair_count].usage = usage;
	p->pair_count++;
}

static uint32_t page_of(const struct parser* p, struct usage_item item)
{
	return item.extended ? item.usage >> 16 : p->global.page;
}

static uint32_t usage_of(struct usage_item item)
{
	return item.extended ? item.usage & 0xffff : item.usage;
}

/* The usages of the main item that is being read: the range, or the list */
static size_t resolve_usages(const struct parser* p, const struct locals* l, uint32_t* pages, uint32_t* usages, size_t max)
{
	size_t n = 0;

	if (l->has_min && l->has_max)
	{
		uint32_t page = page_of(p, l->min), first = usage_of(l->min), last = usage_of(l->max);

		if (last < first)
			return 0;
		for (uint32_t u = first; u <= last && n < max; u++)
		{
			pages[n] = page;
			usages[n++] = u;
			if (u == 0xffffffffu)
				break;
		}
		return n;
	}
	for (size_t i = 0; i < l->usage_count && n < max; i++)
	{
		pages[n] = page_of(p, l->usages[i]);
		usages[n++] = usage_of(l->usages[i]);
	}
	return n;
}

static void fill_common(const struct parser* p, struct iokitd_hid_element* e, int kind, uint32_t flags, int variable)
{
	const struct globals* g = &p->global;

	e->kind = kind;
	e->flags = flags;
	e->report_id = g->report_id;
	e->logical_min = g->logical_min;
	e->logical_max = g->logical_max;
	/* The maximum of a variable item that follows a non-negative minimum is unsigned: 0 .. 0xff is a
	 * range of 256 values, not a minimum above its maximum. Keys (arrays) keep the maximum as it reads. */
	if (variable && g->logical_min >= 0 && g->logical_max < 0)
		e->logical_max = (int32_t) (g->logical_max_raw > 0x7fffffffu ? 0x7fffffffu : g->logical_max_raw);
	e->physical_min = g->physical_min;
	e->physical_max = g->physical_max;
	if (g->physical_min == 0 && g->physical_max == 0)
	{
		e->physical_min = e->logical_min;
		e->physical_max = e->logical_max;
	}
	e->unit = g->unit;
	e->unit_exponent = g->unit_exponent;
}

static void data_item(struct parser* p, const struct locals* l, int kind, uint32_t flags)
{
	const struct globals* g = &p->global;
	uint32_t size = g->report_size, count = g->report_count;
	uint32_t* pages = p->pages;
	uint32_t* usages = p->usages;

	if (size == 0 || count == 0)
		return;
	if ((uint64_t) size * count > MAX_REPORT_BITS)
	{
		p->failed = 1;
		return;
	}

	uint32_t base = take_bits(p, kind, g->report_id, size * count);
	if (p->failed || (flags & IOKITD_HID_FLAG_CONSTANT))
		return;

	int pass_main = kind == IOKITD_HID_INPUT ? PASS_INPUT : kind == IOKITD_HID_OUTPUT ? PASS_OUTPUT : PASS_FEATURE;
	size_t n = resolve_usages(p, l, pages, usages, MAX_USAGES);
	if (n == 0)
		return;

	if (flags & IOKITD_HID_FLAG_VARIABLE)
	{
		uint32_t fields = count < MAX_FIELDS ? count : MAX_FIELDS;
		uint32_t i = 0;

		while (i < fields)
		{
			uint32_t first_page = pages[i < n ? i : n - 1], first = usages[i < n ? i : n - 1];
			uint32_t j = i + 1;
			int equal = 0;

			if (j < fields)
			{
				uint32_t next_page = pages[j < n ? j : n - 1], next = usages[j < n ? j : n - 1];
				if (next_page == first_page && next == first)
					equal = 1;
				else if (next_page == first_page && next == first + 1)
					equal = 0;
				else
					j = i + 1, equal = -1;
			}
			if (j < fields && equal >= 0)
			{
				for (; j < fields; j++)
				{
					uint32_t page = pages[j < n ? j : n - 1], usage = usages[j < n ? j : n - 1];
					if (page != first_page || usage != (equal ? first : first + (j - i)))
						break;
				}
			}
			uint32_t run = j - i;

			struct entry* entry = add_entry(p);
			if (!entry)
				return;
			fill_common(p, &entry->e, kind, flags, 1);
			entry->pass = pass_main;
			entry->cookies = run;
			entry->e.usage_page = first_page;
			entry->e.usage_min = first;
			entry->e.usage_max = equal == 1 ? first : first + run - 1;
			entry->e.duplicates = run > 1 && equal == 1;
			entry->e.size = entry->e.duplicates ? size * run : size;
			entry->e.report_size = size;
			entry->e.report_count = entry->e.duplicates ? run : 1;
			entry->e.bit_offset = base + i * size;
			entry->e.field_count = run;
			i = j;
		}
		return;
	}

	/* An array: its slots, and the elements of the usages that can be in them */
	struct entry* slots = add_entry(p);
	if (!slots)
		return;
	fill_common(p, &slots->e, kind, flags, 0);
	slots->pass = pass_main;
	slots->cookies = count;
	slots->e.array = IOKITD_HID_ARRAY_SLOTS;
	slots->e.duplicates = count > 1;
	slots->e.usage_page = pages[0];
	slots->e.usage_min = slots->e.usage_max = 0xffffffffu;
	slots->e.size = size * count;
	slots->e.report_size = size;
	slots->e.report_count = count;
	slots->e.bit_offset = base;
	slots->e.field_count = count;

	size_t usage_count = n < IOKITD_HID_MAX_ARRAY_USAGES ? n : IOKITD_HID_MAX_ARRAY_USAGES;
	size_t i = 0;
	while (i < usage_count)
	{
		size_t j = i + 1;
		while (j < usage_count && pages[j] == pages[i] && usages[j] == usages[i] + (j - i))
			j++;

		struct entry* entry = add_entry(p);
		if (!entry)
			return;
		fill_common(p, &entry->e, kind, flags, 0);
		entry->pass = kind == IOKITD_HID_INPUT ? PASS_INPUT_USAGES : kind == IOKITD_HID_OUTPUT ? PASS_OUTPUT_USAGES : PASS_FEATURE_USAGES;
		entry->cookies = (uint32_t) (j - i);
		entry->e.array = IOKITD_HID_ARRAY_USAGES;
		entry->e.usage_page = pages[i];
		entry->e.usage_min = usages[i];
		entry->e.usage_max = usages[j - 1];
		entry->e.logical_min = entry->e.physical_min = 0;
		entry->e.logical_max = entry->e.physical_max = 1;
		entry->e.size = entry->e.report_size = 1;
		entry->e.report_count = 1;
		entry->e.slot_bit_offset = base;
		entry->e.slot_size = size;
		entry->e.slot_count = count;
		entry->e.field_count = (uint32_t) (j - i);
		i = j;
	}
}

static void collection_item(struct parser* p, const struct locals* l, uint32_t type)
{
	if (p->depth >= MAX_COLLECTION_DEPTH)
	{
		p->failed = 1;
		return;
	}

	uint32_t page, usages[1], pages[1];
	size_t n = resolve_usages(p, l, pages, usages, 1);
	page = n ? pages[0] : p->global.page;

	struct entry* entry = add_entry(p);
	if (!entry)
		return;
	entry->pass = PASS_INPUT;
	entry->e.kind = IOKITD_HID_COLLECTION;
	entry->e.collection_type = type;
	entry->e.usage_page = page;
	entry->e.usage_min = entry->e.usage_max = n ? usages[0] : 0;
	if (p->depth == 0)
	{
		entry->e.app_page = page;
		entry->e.app_usage = entry->e.usage_min;
		add_pair(p, page, entry->e.usage_min);
	}
	p->open[p->depth] = entry->seq;
	p->open_app_page[p->depth] = p->depth ? p->open_app_page[0] : page;
	p->open_app_usage[p->depth] = p->depth ? p->open_app_usage[0] : entry->e.usage_min;
	p->depth++;
}

static int compare_entries(const void* a, const void* b)
{
	const struct entry* x = a;
	const struct entry* y = b;

	if (x->pass != y->pass)
		return x->pass < y->pass ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static int32_t sign_extend(uint32_t value, int bytes)
{
	if (bytes == 1)
		return (int8_t) value;
	if (bytes == 2)
		return (int16_t) value;
	return (int32_t) value;
}

static int run(struct parser* p, const uint8_t* data, size_t size)
{
	struct locals* L = &p->locals;
	size_t pos = 0;

	memset(L, 0, sizeof(*L));
	memset(&p->global, 0, sizeof(p->global));

	while (pos < size && !p->failed)
	{
		uint8_t prefix = data[pos++];
		int bytes = prefix & 3, type = (prefix >> 2) & 3, tag = prefix >> 4;
		uint32_t value = 0;

		if (prefix == 0xfe) /* long item: a length byte, a tag, and data we do not use */
		{
			if (pos >= size)
				return -1;
			size_t length = data[pos];
			if (pos + 2 + length > size)
				return -1;
			pos += 2 + length;
			continue;
		}
		if (bytes == 3)
			bytes = 4;
		if (pos + bytes > size)
			return -1;
		for (int i = 0; i < bytes; i++)
			value |= (uint32_t) data[pos + i] << (8 * i);
		pos += bytes;

		if (type == 0) /* main */
		{
			if (tag == 0x8)
				data_item(p, L, IOKITD_HID_INPUT, value);
			else if (tag == 0x9)
				data_item(p, L, IOKITD_HID_OUTPUT, value);
			else if (tag == 0xb)
				data_item(p, L, IOKITD_HID_FEATURE, value);
			else if (tag == 0xa)
				collection_item(p, L, value);
			else if (tag == 0xc)
			{
				if (p->depth == 0)
					return -1;
				p->depth--;
			}
			memset(L, 0, sizeof(*L));
		}
		else if (type == 1) /* global */
		{
			struct globals* g = &p->global;

			switch (tag)
			{
				case 0x0: g->page = value; break;
				case 0x1: g->logical_min = sign_extend(value, bytes); break;
				case 0x2:
					g->logical_max = sign_extend(value, bytes);
					g->logical_max_raw = value;
					break;
				case 0x3: g->physical_min = sign_extend(value, bytes); break;
				case 0x4: g->physical_max = sign_extend(value, bytes); break;
				case 0x5: g->unit_exponent = value & 0xf; break;
				case 0x6: g->unit = value; break;
				case 0x7: g->report_size = value; break;
				case 0x8: g->report_id = value & 0xff; break;
				case 0x9: g->report_count = value; break;
				case 0xa: /* push */
					if (p->global_depth >= MAX_COLLECTION_DEPTH)
						return -1;
					p->stack[p->global_depth++] = *g;
					break;
				case 0xb: /* pop */
					if (p->global_depth == 0)
						return -1;
					*g = p->stack[--p->global_depth];
					break;
			}
		}
		else if (type == 2) /* local */
		{
			struct usage_item item = { value, bytes == 4 };

			if (tag == 0x0 && L->usage_count < MAX_USAGES)
				L->usages[L->usage_count++] = item;
			else if (tag == 0x1)
				L->has_min = 1, L->min = item;
			else if (tag == 0x2)
				L->has_max = 1, L->max = item;
		}
	}
	return p->depth == 0 ? 0 : -1;
}

int iokitd_hid_parse(const uint8_t* data, size_t size, struct iokitd_hid_descriptor* out)
{
	struct parser* p = calloc(1, sizeof(*p));
	int result = -1;

	memset(out, 0, sizeof(*out));
	if (!p)
		return -1;

	if (run(p, data, size) < 0 || p->failed || p->report_count == 0)
		goto done;

	/* The report handlers, one per input report, go in after the inputs */
	for (size_t i = 0; i < p->report_count; i++)
	{
		if (p->reports[i].kind != IOKITD_HID_INPUT)
			continue;

		uint32_t bytes = (p->reports[i].bits + 7) / 8 + (p->reports[i].id ? 1 : 0);
		struct entry* entry;

		entry = add_entry(p);
		if (!entry)
			goto done;
		entry->pass = PASS_HANDLER;
		entry->e.kind = IOKITD_HID_HANDLER;
		entry->e.report_id = p->reports[i].id;
		entry->e.flags = IOKITD_HID_FLAG_VARIABLE;
		entry->e.size = bytes * 8;
		entry->e.report_size = 8;
		entry->e.report_count = bytes;
		entry->e.field_count = 1;
	}

	qsort(p->entries, p->entry_count, sizeof(struct entry), compare_entries);

	/* Cookies, and the parents as cookies */
	uint32_t cookie = 1;
	uint32_t* cookie_of = calloc(p->next_seq + 1, sizeof(uint32_t));
	if (!cookie_of)
		goto done;
	for (size_t i = 0; i < p->entry_count; i++)
	{
		struct entry* entry = &p->entries[i];

		entry->e.cookie_min = cookie;
		entry->e.cookie_max = cookie + entry->cookies - 1;
		cookie += entry->cookies;
		cookie_of[entry->seq] = entry->e.cookie_min;
	}
	out->elements = calloc(p->entry_count, sizeof(struct iokitd_hid_element));
	if (!out->elements)
	{
		free(cookie_of);
		goto done;
	}
	for (size_t i = 0; i < p->entry_count; i++)
	{
		out->elements[i] = p->entries[i].e;
		out->elements[i].parent = p->entries[i].parent_seq >= 0 ? cookie_of[p->entries[i].parent_seq] : 0;
	}
	free(cookie_of);
	out->element_count = p->entry_count;
	out->last_cookie = cookie - 1;
	out->reports = p->reports;
	out->report_count = p->report_count;
	out->pairs = p->pairs;
	out->pair_count = p->pair_count;
	p->reports = NULL;
	p->pairs = NULL;
	result = 0;

done:
	free(p->entries);
	free(p->reports);
	free(p->pairs);
	free(p);
	return result;
}

void iokitd_hid_free(struct iokitd_hid_descriptor* descriptor)
{
	free(descriptor->elements);
	free(descriptor->reports);
	free(descriptor->pairs);
	memset(descriptor, 0, sizeof(*descriptor));
}

uint32_t iokitd_hid_report_bytes(const struct iokitd_hid_descriptor* descriptor, int kind, uint32_t id)
{
	for (size_t i = 0; i < descriptor->report_count; i++)
		if (descriptor->reports[i].kind == kind && descriptor->reports[i].id == id)
			return (descriptor->reports[i].bits + 7) / 8;
	return 0;
}

uint32_t iokitd_hid_max_report_bytes(const struct iokitd_hid_descriptor* descriptor, int kind)
{
	uint32_t longest = 0;

	for (size_t i = 0; i < descriptor->report_count; i++)
	{
		if (descriptor->reports[i].kind != kind)
			continue;

		uint32_t bytes = (descriptor->reports[i].bits + 7) / 8 + (descriptor->reports[i].id ? 1 : 0);
		if (bytes > longest)
			longest = bytes;
	}
	return longest;
}
