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

#ifndef IOKITD_IOHIDGAMEPADDEVICE_H
#define IOKITD_IOHIDGAMEPADDEVICE_H

#include "IOService.h"
#include "ServiceRegistry.h"
#include "HostGamepad.h"
#include <string>
#include <vector>

// A game controller of the host, published as an IOHIDDevice service while
// the evdev node exists. It carries the properties IOHIDManager matches and
// reads (usage page and usage, vendor and product, transport, location, the
// report descriptor) and holds the current input report.
//
// Controllers come and go: HostInputMonitor looks at /dev/input once a
// second, publishes controllers it can read, and unpublishes them when the
// node disappears or reading fails. Without permission to read the nodes no
// controller is published, which is not an error.
class IOHIDGamepadDevice : public IOService
{
public:
	explicit IOHIDGamepadDevice(const struct iokitd_gamepad& pad);
	~IOHIDGamepadDevice();

	const char* className() const override;
	bool conformsTo(const char* className) override;
	NSDictionary* matchingDictionary() override;
	NSDictionary* getProperties() override;

	std::string path() const { return m_pad.path; }
	const struct iokitd_gamepad& description() const { return m_pad; }
	const std::vector<uint8_t>& report() const { return m_report; }

	// Starts reading events. Calls onGone() when the device went away.
	void startReading(void (^onGone)(IOHIDGamepadDevice*));
	void stopReading();

private:
	struct iokitd_gamepad m_pad;
	std::vector<uint8_t> m_report;
	NSDictionary* m_properties;
	int m_fd = -1;
	dispatch_source_t m_readSource = nullptr;
};

#endif
