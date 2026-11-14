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

#include "HostInputMonitor.h"
#include "IOHIDGamepadDevice.h"
#include "IOHIDInputDevice.h"
#include <os/log.h>
#include <functional>
#include <map>
#include <set>
#include <string>

static const char* kHostInputDirectory = "/dev/input";

namespace {

// Nodes that were looked at and are not a device we publish are looked at again a few times:
// the permissions of a new node (udev) can lag behind its creation
const int kProbeAttempts = 5;

struct Published
{
	IOService* service;
	std::function<void()> stopReading;
};

struct Monitor
{
	ServiceRegistry* registry;
	std::map<std::string, Published> devices;
	std::map<std::string, int> ignored;
	dispatch_source_t timer;

	void scan();
	void remove(const std::string& path);

	template <class Device, class Description>
	void publish(const std::string& path, const Description& description, const char* name, const char* kind);
};

void Monitor::remove(const std::string& path)
{
	auto it = devices.find(path);
	if (it == devices.end())
		return;

	Published published = it->second;
	devices.erase(it);
	published.stopReading();
	published.service->detachFromPlane(kIOServicePlane);
	registry->unregisterService(published.service);
	os_log(OS_LOG_DEFAULT, "iokitd: input device %{public}s removed", path.c_str());
}

template <class Device, class Description>
void Monitor::publish(const std::string& path, const Description& description, const char* name, const char* kind)
{
	Device* device = new Device(description);

	devices[path] = Published{ device, [device]() { device->stopReading(); } };
	ignored.erase(path);
	device->registerInPlane(kIOServicePlane, name[0] ? name : "IOHIDDevice", IORegistryEntry::root());
	registry->registerService(device);
	device->startReading(^(Device* gone) { remove(gone->path()); });
	os_log(OS_LOG_DEFAULT, "iokitd: %{public}s %{public}s (%{public}s)", kind, path.c_str(), name);
}

void Monitor::scan()
{
	static char paths[64][64];
	int count = iokitd_gamepad_list(kHostInputDirectory, paths, 64);
	std::set<std::string> present;

	for (int i = 0; i < count; i++)
		present.insert(paths[i]);

	for (auto it = devices.begin(); it != devices.end();)
	{
		std::string path = (it++)->first;
		if (!present.count(path))
			remove(path);
	}
	for (auto it = ignored.begin(); it != ignored.end();)
	{
		if (!present.count(it->first))
			it = ignored.erase(it);
		else
			++it;
	}

	for (const std::string& path : present)
	{
		if (devices.count(path))
			continue;
		auto skip = ignored.find(path);
		if (skip != ignored.end() && skip->second >= kProbeAttempts)
			continue;

		struct iokitd_gamepad pad;
		struct iokitd_input input;

		if (iokitd_gamepad_probe(path.c_str(), &pad) == 0)
			publish<IOHIDGamepadDevice>(path, pad, pad.name, "game controller");
		else if (iokitd_input_probe(path.c_str(), &input) == 0)
			publish<IOHIDInputDevice>(path, input, input.name, input.kind == IOKITD_INPUT_KEYBOARD ? "keyboard" : "mouse");
		else
			ignored[path]++;
	}
}

}

void HostInputMonitor::start(ServiceRegistry* registry)
{
	static Monitor* monitor = new Monitor;

	monitor->registry = registry;
	monitor->scan();
	monitor->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
	dispatch_source_set_timer(monitor->timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC, NSEC_PER_SEC / 10);
	dispatch_source_set_event_handler(monitor->timer, ^{ monitor->scan(); });
	dispatch_resume(monitor->timer);
}
