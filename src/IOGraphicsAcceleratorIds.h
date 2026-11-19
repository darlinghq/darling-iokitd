/*
 This file is part of Darling.

 Copyright (C) 2026 Darling developers

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

#ifndef IOKITD_IOGRAPHICSACCELERATORIDS_H
#define IOKITD_IOGRAPHICSACCELERATORIDS_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

// The GPU identity iokitd publishes comes from the same inputs
// tools/darling-system_profiler.sh uses for SPDisplaysDataType, so both report
// the same PCI IDs in the host and mac profiles. Free of Foundation so the host
// tests can compile this header directly.
namespace IOGraphicsAcceleratorIds {

struct PciId
{
	uint32_t vendor;
	uint32_t device;
};

inline std::string systemRoot()
{
	const char* root = getenv("RCLOL_SYSTEM_ROOT");
	return root && *root ? std::string(root) : std::string("/Volumes/SystemRoot");
}

inline std::string systemProfilePath()
{
	const char* path = getenv("RCLOL_SYSTEM_PROP");
	return path && *path ? std::string(path) : std::string("/private/etc/darling-system.prop");
}

inline bool readPciId(const std::string& directory, const char* name, uint32_t* value)
{
	FILE* file = fopen((directory + "/" + name).c_str(), "r");
	if (!file)
		return false;

	unsigned int parsed = 0;
	bool ok = fscanf(file, "%x", &parsed) == 1;
	fclose(file);
	if (ok)
		*value = parsed;
	return ok;
}

inline bool isCardName(const char* name)
{
	if (strncmp(name, "card", 4) != 0 || name[4] == '\0')
		return false;
	for (const char* c = name + 4; *c; c++)
		if (!isdigit((unsigned char) *c))
			return false;
	return true;
}

// Every DRM card with a readable vendor/device pair, in the driver's probe
// order. A missing root or an unreadable card leaves the identity unknown
// instead of falling back to another device.
inline std::vector<PciId> hostCards()
{
	std::vector<PciId> ids;
	const std::string roots[] = {
		systemRoot() + "/sys/class/drm",
		"/sys/class/drm",
	};

	for (const std::string& root : roots)
	{
		DIR* directory = opendir(root.c_str());
		if (!directory)
			continue;

		std::vector<std::string> cards;
		while (struct dirent* entry = readdir(directory))
			if (isCardName(entry->d_name))
				cards.emplace_back(entry->d_name);
		closedir(directory);

		std::sort(cards.begin(), cards.end(), [](const std::string& left, const std::string& right) {
			return atoi(left.c_str() + 4) < atoi(right.c_str() + 4);
		});

		for (const std::string& card : cards)
		{
			uint32_t vendor = 0, device = 0;
			std::string deviceDirectory = root + "/" + card + "/device";
			if (readPciId(deviceDirectory, "vendor", &vendor)
			    && readPciId(deviceDirectory, "device", &device))
				ids.push_back({ vendor, device });
		}
		if (!ids.empty())
			return ids;
	}
	return ids;
}

// Returns the last `key=value` line, like the sed/tail pair system_profiler
// uses. `found` distinguishes a missing key from an empty value.
inline void readLastValue(const std::string& path, const std::string& key,
                          std::string* value, bool* found)
{
	*value = std::string();
	*found = false;
	FILE* file = fopen(path.c_str(), "r");
	if (!file)
		return;

	const std::string prefix = key + "=";
	char line[256];
	while (fgets(line, sizeof(line), file))
	{
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();
		if (text.compare(0, prefix.size(), prefix) == 0)
		{
			*value = text.substr(prefix.size());
			*found = true;
		}
	}
	fclose(file);
}

// The effective GPU name of the active profile, resolved like
// tools/darling-system_profiler.sh: RCLOL_GPU_NAME, then the last
// <profile>.gpu.name line, then DARLING_METAL_MAC_GPU_NAME, then the host.
// An empty value therefore means host, and only "mac" substitutes the
// compatibility PCI IDs.
inline std::string hostGpuMode()
{
	std::string profile = "compat";
	std::string name;
	const char* profileEnv = getenv("RCLOL_PLATFORM_PROFILE");
	const char* nameEnv = getenv("RCLOL_GPU_NAME");
	if (nameEnv)
		name = nameEnv;

	// system_profiler only reads the profile file when the launcher did not
	// hand RCLOL_PLATFORM_PROFILE over.
	if (!(profileEnv && *profileEnv))
	{
		std::string value;
		bool found = false;
		readLastValue(systemProfilePath(), "profile", &value, &found);
		if (found && (value == "host" || value == "compat"))
			profile = value;
		if (name.empty())
			readLastValue(systemProfilePath(), profile + ".gpu.name", &name, &found);
	}

	if (name.empty())
	{
		const char* mac = getenv("DARLING_METAL_MAC_GPU_NAME");
		if (mac && *mac && std::strcmp(mac, "0") != 0)
			name = "mac";
	}
	return name.empty() ? std::string("host") : name;
}

// Metal chooses a Vulkan device: /tmp/metal-gpu first, then METAL_INDIUM_GPU,
// then the loader's order (see deps/indium device.cpp). Only the explicit
// selector can be tied back to a DRM card.
inline bool readGpuSelector(std::string* selector)
{
	if (FILE* file = fopen("/tmp/metal-gpu", "r"))
	{
		char buffer[256] = { 0 };
		char* got = fgets(buffer, sizeof(buffer), file);
		fclose(file);
		if (got)
		{
			std::string value(buffer);
			while (!value.empty() && (value.back() == '\n' || value.back() == '\r'
			                          || value.back() == ' ' || value.back() == '\t'))
				value.pop_back();
			if (!value.empty())
			{
				*selector = value;
				return true;
			}
		}
	}

	const char* env = getenv("METAL_INDIUM_GPU");
	if (env && *env)
	{
		*selector = env;
		return true;
	}
	return false;
}

inline bool containsIgnoreCase(const std::string& text, const char* needle)
{
	return std::search(text.begin(), text.end(), needle, needle + std::strlen(needle),
	                   [](char left, char right) {
		return std::tolower((unsigned char) left) == std::tolower((unsigned char) right);
	}) != text.end();
}

// The selector names a Vulkan device, not a card, so only a unique vendor hint
// can be mapped back. A numeric selector counts Vulkan devices, and the
// loader's order need not match the driver's card order, so it stays unknown;
// two cards of the hinted vendor stay unknown as well. A single card is only
// unambiguous without a selector: one can still name a software device.
inline bool cardForSelector(const std::vector<PciId>& cards, const std::string& selector,
                            PciId* selected)
{
	if (cards.size() == 1 && selector.empty())
	{
		*selected = cards.front();
		return true;
	}
	if (cards.empty() || selector.empty()
	    || std::all_of(selector.begin(), selector.end(),
	                   [](unsigned char c) { return std::isdigit(c) != 0; }))
		return false;

	static const struct { const char* name; uint32_t vendor; } vendors[] = {
		{ "nvidia", 0x10de },
		{ "amd", 0x1002 },
		{ "radeon", 0x1002 },
		{ "intel", 0x8086 },
	};
	std::vector<uint32_t> matches;
	for (const auto& vendor : vendors)
		if (containsIgnoreCase(selector, vendor.name)
		    && std::find(matches.begin(), matches.end(), vendor.vendor) == matches.end())
			matches.push_back(vendor.vendor);
	if (matches.size() != 1)
		return false;

	const PciId* found = nullptr;
	for (const PciId& card : cards)
		if (card.vendor == matches.front())
		{
			if (found)
				return false;
			found = &card;
		}
	if (!found)
		return false;
	*selected = *found;
	return true;
}

// Pairs a card list, selector and mode with the identity to publish. Split
// from acceleratorIds() so the host tests can exercise every combination.
inline PciId acceleratorIdsFor(const std::vector<PciId>& cards, const std::string& selector,
                               const std::string& mode)
{
	PciId selected = { 0, 0 };
	if (!cardForSelector(cards, selector, &selected))
		return { 0, 0 };

	// Matches system_profiler: mac mode presents the MacBookPro16,1 AMD and
	// Intel GPUs, an empty or host name keeps the host identity, and NVIDIA
	// keeps its own IDs in both profiles.
	if (mode != "mac" || (selected.vendor != 0x1002 && selected.vendor != 0x8086))
		return selected;

	if (selected.vendor == 0x8086)
		return { 0x8086, 0x3e9b };
	return { 0x1002, 0x7340 };
}

inline PciId acceleratorIds()
{
	std::string selector;
	readGpuSelector(&selector);
	return acceleratorIdsFor(hostCards(), selector, hostGpuMode());
}

}

#endif
