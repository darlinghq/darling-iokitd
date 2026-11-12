/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Host test of HostDisplay.c. The EDID blocks are synthesised here with
 * made-up identifiers (manufacturer "XYZ"), laid out as in the VESA
 * Enhanced EDID standard, version 1.4. The sysfs tree is a temporary
 * directory given as the first argument.
 */
#include "../src/HostDisplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s (line %d)\n", #c, __LINE__); failed = 1; } } while (0)

static void fix_checksum(uint8_t* edid)
{
	uint8_t sum = 0;
	edid[127] = 0;
	for (int i = 0; i < 127; i++)
		sum += edid[i];
	edid[127] = (uint8_t) (256 - sum);
}

static void descriptor_text(uint8_t* d, uint8_t tag, const char* text)
{
	memset(d, 0, 18);
	d[3] = tag;
	memset(d + 5, ' ', 13);
	size_t n = strlen(text);
	memcpy(d + 5, text, n);
	if (n < 13)
		d[5 + n] = 0x0a;
}

// "XYZ", product 0x1234, serial 0x01020304, week 7 of 2021, 531x299 mm, named "Synthetic 27"
static void make_edid(uint8_t* edid)
{
	memset(edid, 0, 128);
	edid[1] = edid[2] = edid[3] = edid[4] = edid[5] = edid[6] = 0xff;
	// X=24, Y=25, Z=26: 0 11000 11001 11010
	uint16_t vendor = (24 << 10) | (25 << 5) | 26;
	edid[8] = vendor >> 8;
	edid[9] = vendor & 0xff;
	edid[10] = 0x34; edid[11] = 0x12;
	edid[12] = 0x04; edid[13] = 0x03; edid[14] = 0x02; edid[15] = 0x01;
	edid[16] = 7;
	edid[17] = 2021 - 1990;
	edid[18] = 1; edid[19] = 4;
	edid[20] = 0x80;			// digital input
	edid[21] = 53; edid[22] = 30;	// centimetres
	// preferred timing: a clock, and the size in mm in bytes 12 to 14
	edid[54] = 0x01; edid[55] = 0x01;
	edid[54 + 12] = 531 & 0xff;
	edid[54 + 13] = 299 & 0xff;
	edid[54 + 14] = (uint8_t) (((531 >> 8) << 4) | (299 >> 8));
	descriptor_text(edid + 72, 0xfc, "Synthetic 27");
	descriptor_text(edid + 90, 0xff, "SN-0001");
	fix_checksum(edid);
}

static void write_file(const char* dir, const char* connector, const char* file, const void* data, size_t length)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, connector);
	char command[1100];
	snprintf(command, sizeof(command), "mkdir -p '%s'", path);
	if (system(command) != 0)
		exit(2);
	snprintf(path, sizeof(path), "%s/%s/%s", dir, connector, file);
	FILE* f = fopen(path, "wb");
	if (!f)
		exit(2);
	fwrite(data, 1, length, f);
	fclose(f);
}

static void test_edid(void)
{
	uint8_t edid[128];
	host_edid_info info;

	make_edid(edid);
	CHECK(host_edid_parse(edid, sizeof(edid), &info) == 0);
	CHECK(strcmp(info.manufacturer, "XYZ") == 0);
	CHECK(info.vendorID == 0x633a);
	CHECK(info.productID == 0x1234);
	CHECK(info.serialNumber == 0x01020304);
	CHECK(info.week == 7);
	CHECK(info.year == 2021);
	CHECK(strcmp(info.productName, "Synthetic 27") == 0);
	CHECK(strcmp(info.serialString, "SN-0001") == 0);
	CHECK(info.widthMM == 531 && info.heightMM == 299);
	CHECK(info.digital == 1);

	// a longer EDID (extension blocks) still parses its base block
	uint8_t longer[256];
	memset(longer, 0, sizeof(longer));
	memcpy(longer, edid, 128);
	CHECK(host_edid_parse(longer, sizeof(longer), &info) == 0);

	// no preferred timing size: whole centimetres
	uint8_t cm[128];
	memcpy(cm, edid, 128);
	memset(cm + 54, 0, 18);
	fix_checksum(cm);
	CHECK(host_edid_parse(cm, sizeof(cm), &info) == 0);
	CHECK(info.widthMM == 530 && info.heightMM == 300);

	// an aspect ratio instead of a size (projector): unknown size
	uint8_t ratio[128];
	memcpy(ratio, cm, 128);
	ratio[21] = 0; ratio[22] = 0;
	fix_checksum(ratio);
	CHECK(host_edid_parse(ratio, sizeof(ratio), &info) == 0);
	CHECK(info.widthMM == 0 && info.heightMM == 0);

	// week 0xff marks a model year
	uint8_t model[128];
	memcpy(model, edid, 128);
	model[16] = 0xff;
	fix_checksum(model);
	CHECK(host_edid_parse(model, sizeof(model), &info) == 0);
	CHECK(info.week == 0 && info.year == 2021);

	// a name of exactly 13 characters has no terminator
	uint8_t full[128];
	memcpy(full, edid, 128);
	memset(full + 72, 0, 18);
	full[72 + 3] = 0xfc;
	memcpy(full + 72 + 5, "ABCDEFGHIJKLM", 13);
	fix_checksum(full);
	CHECK(host_edid_parse(full, sizeof(full), &info) == 0);
	CHECK(strcmp(info.productName, "ABCDEFGHIJKLM") == 0);

	// the error cases
	CHECK(host_edid_parse(edid, 127, &info) == -1);
	CHECK(host_edid_parse(NULL, 128, &info) == -1);
	CHECK(host_edid_parse(edid, 128, NULL) == -1);
	uint8_t bad[128];
	memcpy(bad, edid, 128);
	bad[20] ^= 1;	// checksum no longer matches
	CHECK(host_edid_parse(bad, sizeof(bad), &info) == -1);
	memcpy(bad, edid, 128);
	bad[0] = 1;
	fix_checksum(bad);
	CHECK(host_edid_parse(bad, sizeof(bad), &info) == -1);
	memset(bad, 0, sizeof(bad));
	CHECK(host_edid_parse(bad, sizeof(bad), &info) == -1);
}

static void test_connectors(const char* dir)
{
	uint8_t edid[256];
	host_drm_connector found[8];

	make_edid(edid);
	// connected and enabled, with an EDID that has an extension block
	memset(edid + 128, 0, 128);
	write_file(dir, "card0-eDP-1", "status", "connected\n", 10);
	write_file(dir, "card0-eDP-1", "enabled", "enabled\n", 8);
	write_file(dir, "card0-eDP-1", "edid", edid, 256);
	// connected, no "enabled" attribute and an empty EDID
	write_file(dir, "card1-HDMI-A-2", "status", "connected\n", 10);
	write_file(dir, "card1-HDMI-A-2", "edid", "", 0);
	// connected but not driven by a CRTC
	write_file(dir, "card0-DP-3", "status", "connected\n", 10);
	write_file(dir, "card0-DP-3", "enabled", "disabled\n", 9);
	// not connected
	write_file(dir, "card0-HDMI-A-1", "status", "disconnected\n", 13);
	write_file(dir, "card0-HDMI-A-1", "enabled", "disabled\n", 9);
	// not connectors
	write_file(dir, "card0", "status", "connected\n", 10);
	write_file(dir, "renderD128", "status", "connected\n", 10);
	write_file(dir, "version", "status", "connected\n", 10);

	size_t count = host_drm_connectors(dir, found, 8);
	CHECK(count == 2);
	if (count == 2)
	{
		// sorted by connector name
		CHECK(strcmp(found[0].name, "HDMI-A-2") == 0);
		CHECK(found[0].edid == NULL && found[0].edidLength == 0);
		CHECK(found[0].enabled == 1);
		CHECK(strcmp(found[1].name, "eDP-1") == 0);
		CHECK(found[1].edid != NULL && found[1].edidLength == 256);
		host_edid_info info;
		CHECK(found[1].edid && host_edid_parse(found[1].edid, found[1].edidLength, &info) == 0);
		host_drm_connector_free(&found[0]);
		host_drm_connector_free(&found[1]);
	}

	// the limit
	CHECK(host_drm_connectors(dir, found, 1) == 1);
	host_drm_connector_free(&found[0]);
	CHECK(host_drm_connectors("/nonexistent-directory-for-test", found, 8) == 0);
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	test_edid();
	test_connectors(argv[1]);
	if (!failed)
		printf("PASS\n");
	return failed;
}
