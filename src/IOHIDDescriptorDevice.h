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

#ifndef IOKITD_IOHIDDESCRIPTORDEVICE_H
#define IOKITD_IOHIDDESCRIPTORDEVICE_H

#include "IOService.h"
#include "ServiceRegistry.h"
#include "HostHIDDevice.h"
#include "IOHIDLibUserClient.h"
#include <map>
#include <string>
#include <vector>

// What a node of the host is: the identity evdev reports for it and the
// HID device (the report descriptor the host has for it, mapped onto the
// events of the node) that makes its reports.
struct IOHIDDescriptorSource
{
	std::string path;
	std::string name;
	std::string serial;
	uint16_t bustype = 0, vendor = 0, product = 0, version = 0;
	bool hasLeds = false;                   // the node has keyboard LEDs (EV_LED)
	std::vector<uint8_t> descriptor;        // the host's report descriptor
	struct iokitd_hid_device* device = nullptr;  // owned by the IOHIDDescriptorDevice made from this
};

// A game controller, keyboard or mouse of the host that is a HID device
// there, published as an IOHIDDevice service with the report descriptor and
// elements of the real device (HostHIDDevice.h), while the evdev node
// exists. Devices without a HID device behind their node (virtual ones, for
// example) are published as IOHIDGamepadDevice or IOHIDInputDevice, which
// describe the node with a report descriptor of their own.
//
// Output reports: the keyboard LED fields of the output reports are written
// to the node. Any other output report, and feature reports, need the
// device's own interface (hidraw), which iokitd does not open, and are
// refused with kIOReturnUnsupported.
class IOHIDDescriptorDevice : public IOService, public IOHIDReportDevice
{
public:
	explicit IOHIDDescriptorDevice(const IOHIDDescriptorSource& source);
	~IOHIDDescriptorDevice();

	const char* className() const override;
	bool conformsTo(const char* className) override;
	NSDictionary* matchingDictionary() override;
	bool matches(NSDictionary* dict) override;
	NSDictionary* getProperties() override;
	IOReturn newUserClient(uint32_t type, IOUserClient** client) override;

	// IOHIDReportDevice
	const std::vector<IOHIDElementDescription>& hidElements() const override { return m_elements; }
	const std::vector<uint8_t>& hidReport() const override;
	const std::vector<uint8_t>* hidInputReport(uint32_t reportID) const override;
	size_t hidMaxInputReportSize() const override;
	uint32_t hidInputReportID() const override;
	const std::vector<uint8_t>* hidOutputReport(uint32_t reportID) const override;
	IOReturn hidSetReport(uint32_t type, uint32_t reportID, const uint8_t* data, size_t length) override;

	std::string path() const { return m_path; }

	// Starts reading events. Calls onGone() when the device went away.
	void startReading(void (^onGone)(IOHIDDescriptorDevice*));
	void stopReading();

	// Makes the device for a node that is a game controller, keyboard or mouse (identity from its evdev
	// probe), or returns false when the host has no usable report descriptor for it: no HID device behind
	// the node, another published node on the same HID device, force feedback (whose output reports need
	// the device's own interface), or no field the node can fill.
	static bool probe(const std::string& path, const std::string& name, const std::string& serial,
		uint16_t bustype, uint16_t vendor, uint16_t product, uint16_t version, bool rumble, bool leds, IOHIDDescriptorSource* source);

private:
	std::string m_path;
	bool m_hasLeds;
	struct iokitd_hid_device* m_device;
	std::vector<std::vector<uint8_t>> m_inputs;         // by input report index of the device
	std::map<uint32_t, std::vector<uint8_t>> m_outputs; // the output reports last sent, without their ID
	std::vector<IOHIDElementDescription> m_elements;
	NSDictionary* m_properties;
	int m_fd = -1;
	dispatch_source_t m_readSource = nullptr;

};

#endif
