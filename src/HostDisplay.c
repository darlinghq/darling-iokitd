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

#include "HostDisplay.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Offsets in the base block, from the VESA Enhanced EDID standard
#define EDID_MANUFACTURER 8
#define EDID_PRODUCT 10
#define EDID_SERIAL 12
#define EDID_WEEK 16
#define EDID_YEAR 17
#define EDID_INPUT 20
#define EDID_WIDTH_CM 21
#define EDID_HEIGHT_CM 22
#define EDID_DESCRIPTORS 54
#define EDID_DESCRIPTOR_SIZE 18

static void copy_descriptor_text(char* out, const uint8_t* descriptor)
{
	// 13 characters, terminated by a line feed and padded with spaces
	int length = 0;
	for (int i = 5; i < EDID_DESCRIPTOR_SIZE && descriptor[i] != 0x0a; i++)
		out[length++] = (descriptor[i] >= 0x20 && descriptor[i] < 0x7f) ? (char) descriptor[i] : ' ';
	while (length > 0 && out[length - 1] == ' ')
		length--;
	out[length] = '\0';
}

int host_edid_parse(const uint8_t* edid, size_t length, host_edid_info* info)
{
	static const uint8_t header[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };

	if (!edid || !info || length < HOST_EDID_BLOCK_SIZE || memcmp(edid, header, sizeof(header)) != 0)
		return -1;

	uint8_t sum = 0;
	for (int i = 0; i < HOST_EDID_BLOCK_SIZE; i++)
		sum += edid[i];
	if (sum != 0)
		return -1;

	memset(info, 0, sizeof(*info));

	// three 5-bit letters, 1 = 'A'
	info->vendorID = (uint16_t) ((edid[EDID_MANUFACTURER] << 8) | edid[EDID_MANUFACTURER + 1]);
	info->manufacturer[0] = (char) ('@' + ((info->vendorID >> 10) & 0x1f));
	info->manufacturer[1] = (char) ('@' + ((info->vendorID >> 5) & 0x1f));
	info->manufacturer[2] = (char) ('@' + (info->vendorID & 0x1f));

	info->productID = (uint16_t) (edid[EDID_PRODUCT] | (edid[EDID_PRODUCT + 1] << 8));
	info->serialNumber = (uint32_t) edid[EDID_SERIAL] | ((uint32_t) edid[EDID_SERIAL + 1] << 8)
		| ((uint32_t) edid[EDID_SERIAL + 2] << 16) | ((uint32_t) edid[EDID_SERIAL + 3] << 24);

	// 0xff in the week field marks the year as the model year
	info->week = (edid[EDID_WEEK] == 0xff) ? 0 : edid[EDID_WEEK];
	info->year = 1990 + edid[EDID_YEAR];
	info->digital = (edid[EDID_INPUT] & 0x80) != 0;

	for (int i = 0; i < 4; i++)
	{
		const uint8_t* descriptor = edid + EDID_DESCRIPTORS + i * EDID_DESCRIPTOR_SIZE;

		if (descriptor[0] == 0 && descriptor[1] == 0)
		{
			// a display descriptor, not a timing
			if (descriptor[3] == 0xfc)
				copy_descriptor_text(info->productName, descriptor);
			else if (descriptor[3] == 0xff)
				copy_descriptor_text(info->serialString, descriptor);
		}
		else if (i == 0)
		{
			// the preferred timing carries the image size in millimetres
			info->widthMM = descriptor[12] | ((descriptor[14] & 0xf0) << 4);
			info->heightMM = descriptor[13] | ((descriptor[14] & 0x0f) << 8);
		}
	}

	if (info->widthMM == 0 || info->heightMM == 0)
	{
		// whole centimetres; both 0 (or either 0: aspect ratio) means unknown
		if (edid[EDID_WIDTH_CM] != 0 && edid[EDID_HEIGHT_CM] != 0)
		{
			info->widthMM = edid[EDID_WIDTH_CM] * 10u;
			info->heightMM = edid[EDID_HEIGHT_CM] * 10u;
		}
		else
		{
			info->widthMM = info->heightMM = 0;
		}
	}
	return 0;
}

static int read_line(const char* path, char* out, size_t size)
{
	FILE* file = fopen(path, "r");
	if (!file)
		return -1;
	char* got = fgets(out, (int) size, file);
	fclose(file);
	if (!got)
		return -1;
	out[strcspn(out, "\r\n")] = '\0';
	return 0;
}

static uint8_t* read_binary(const char* path, size_t* length)
{
	// sysfs binary attributes report a size of 0, so read until the end
	FILE* file = fopen(path, "rb");
	*length = 0;
	if (!file)
		return NULL;

	size_t capacity = 512;
	uint8_t* data = malloc(capacity);
	while (data)
	{
		size_t got = fread(data + *length, 1, capacity - *length, file);
		*length += got;
		if (*length < capacity)
			break;
		if (capacity >= 32 * 1024)
			break;
		capacity *= 2;
		uint8_t* grown = realloc(data, capacity);
		if (!grown)
		{
			free(data);
			data = NULL;
			break;
		}
		data = grown;
	}
	fclose(file);

	if (data && *length == 0)
	{
		free(data);
		data = NULL;
	}
	return data;
}

// "cardN-<connector>": a digit run after "card", then a dash
static const char* connector_name(const char* entry)
{
	if (strncmp(entry, "card", 4) != 0 || entry[4] < '0' || entry[4] > '9')
		return NULL;
	const char* c = entry + 4;
	while (*c >= '0' && *c <= '9')
		c++;
	if (*c != '-' || c[1] == '\0')
		return NULL;
	return c + 1;
}

static int compare_connectors(const void* a, const void* b)
{
	return strcmp(((const host_drm_connector*) a)->name, ((const host_drm_connector*) b)->name);
}

size_t host_drm_connectors(const char* sysfsDirectory, host_drm_connector* out, size_t max)
{
	DIR* directory = opendir(sysfsDirectory);
	size_t count = 0;
	if (!directory)
		return 0;

	while (count < max)
	{
		struct dirent* entry = readdir(directory);
		if (!entry)
			break;

		const char* name = connector_name(entry->d_name);
		if (!name)
			continue;

		char path[1024], line[64];
		snprintf(path, sizeof(path), "%s/%s/status", sysfsDirectory, entry->d_name);
		if (read_line(path, line, sizeof(line)) != 0 || strcmp(line, "connected") != 0)
			continue;

		// "enabled" tells whether a CRTC drives the connector. A kernel that lacks the attribute
		// is taken as enabled: only an explicit "disabled" leaves the connector out.
		int enabled = 1;
		snprintf(path, sizeof(path), "%s/%s/enabled", sysfsDirectory, entry->d_name);
		if (read_line(path, line, sizeof(line)) == 0 && strcmp(line, "disabled") == 0)
			enabled = 0;
		if (!enabled)
			continue;

		host_drm_connector* connector = &out[count++];
		memset(connector, 0, sizeof(*connector));
		snprintf(connector->name, sizeof(connector->name), "%s", name);
		connector->enabled = enabled;
		snprintf(path, sizeof(path), "%s/%s/edid", sysfsDirectory, entry->d_name);
		connector->edid = read_binary(path, &connector->edidLength);
	}
	closedir(directory);

	// readdir order is arbitrary; keep the order of the displays stable
	qsort(out, count, sizeof(*out), compare_connectors);
	return count;
}

void host_drm_connector_free(host_drm_connector* connector)
{
	free(connector->edid);
	connector->edid = NULL;
	connector->edidLength = 0;
}
