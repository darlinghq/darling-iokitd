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

#include "IOHIDInputDevice.h"
#include "IOHIDDeviceMatching.h"
#include "IOUserClient.h"
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>
#include <IOKit/IOKitKeys.h>
#include <os/log.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

// The values of IOHIDKeys.h
static NSString* const kKeyTransport = @"Transport";
static NSString* const kKeyVendorID = @"VendorID";
static NSString* const kKeyProductID = @"ProductID";
static NSString* const kKeyVersionNumber = @"VersionNumber";
static NSString* const kKeyProduct = @"Product";
static NSString* const kKeySerialNumber = @"SerialNumber";
static NSString* const kKeyLocationID = @"LocationID";
static NSString* const kKeyPrimaryUsagePage = @"PrimaryUsagePage";
static NSString* const kKeyPrimaryUsage = @"PrimaryUsage";
static NSString* const kKeyDeviceUsagePairs = @"DeviceUsagePairs";
static NSString* const kKeyDeviceUsagePage = @"DeviceUsagePage";
static NSString* const kKeyDeviceUsage = @"DeviceUsage";
static NSString* const kKeyReportDescriptor = @"ReportDescriptor";
static NSString* const kKeyMaxInputReportSize = @"MaxInputReportSize";
static NSString* const kKeyMaxOutputReportSize = @"MaxOutputReportSize";
static NSString* const kKeyMaxFeatureReportSize = @"MaxFeatureReportSize";

static NSString* const kKeyPlugInTypes = @"IOCFPlugInTypes";

// The plug-in that IOHIDDeviceCreate() loads (kIOHIDDeviceTypeID, and the
// plug-in's path relative to the extensions folder)
static NSString* const kHIDDeviceTypeID = @"7DDEECA8-A7B4-11DA-8A0E-0014519758EF";
static NSString* const kHIDLibPlugIn = @"IOHIDFamily.kext/Contents/PlugIns/IOHIDLib.plugin";

static NSString* transportName(uint16_t bustype)
{
	// BUS_USB, BUS_BLUETOOTH, BUS_I8042 (the built-in keyboard and pointer of a PC)
	switch (bustype)
	{
		case 0x03: return @"USB";
		case 0x05: return @"Bluetooth";
		case 0x11: return @"PS/2";
		default: return @"Virtual";
	}
}

static NSDictionary* propertiesOf(const struct iokitd_input& dev)
{
	uint8_t descriptor[256];
	int length = iokitd_input_report_descriptor(&dev, descriptor, sizeof(descriptor));
	NSMutableDictionary* props = [NSMutableDictionary dictionary];
	const char* fallbackName = dev.kind == IOKITD_INPUT_KEYBOARD ? "Keyboard" : "Mouse";

	props[kKeyPlugInTypes] = @{ kHIDDeviceTypeID: kHIDLibPlugIn };
	props[kKeyTransport] = transportName(dev.bustype);
	props[kKeyVendorID] = @(dev.vendor);
	props[kKeyProductID] = @(dev.product);
	props[kKeyVersionNumber] = @(dev.version);
	props[kKeyProduct] = [NSString stringWithUTF8String: dev.name[0] ? dev.name : fallbackName];
	if (dev.serial[0])
		props[kKeySerialNumber] = [NSString stringWithUTF8String: dev.serial];

	// The number of the event node identifies the device on this machine
	const char* base = std::strrchr(dev.path, '/');
	props[kKeyLocationID] = @(base ? std::atoi(base + 6) : 0);

	props[kKeyPrimaryUsagePage] = @1;
	props[kKeyPrimaryUsage] = @(iokitd_input_primary_usage(&dev));
	// A mouse is matched as a Mouse and as the Pointer inside it
	NSMutableArray* pairs = [NSMutableArray arrayWithObject: @{ kKeyDeviceUsagePage: @1, kKeyDeviceUsage: @(iokitd_input_primary_usage(&dev)) }];
	if (dev.kind == IOKITD_INPUT_MOUSE)
		[pairs addObject: @{ kKeyDeviceUsagePage: @1, kKeyDeviceUsage: @1 }];
	props[kKeyDeviceUsagePairs] = pairs;
	if (length > 0)
		props[kKeyReportDescriptor] = [NSData dataWithBytes: descriptor length: length];
	props[kKeyMaxInputReportSize] = @(dev.report_size);
	props[kKeyMaxOutputReportSize] = @(dev.has_leds ? 1 : 0);
	props[kKeyMaxFeatureReportSize] = @0;
	return props;
}

// The elements of the report descriptor iokitd generates (HostInput.h), cookies from 1 in descriptor order:
// the application collection; for a keyboard the modifiers (usages 0xE0 .. 0xE7, bit 0 first), the keys
// (usages 4 .. 0x73, one bit each) and the LEDs of the output report; for a mouse the Pointer collection,
// the buttons, X, Y, the wheel and AC Pan (relative values); and the input report as a whole.
static std::vector<IOHIDElementDescription> elementsOf(const struct iokitd_input& dev)
{
	std::vector<IOHIDElementDescription> elements;
	uint32_t cookie = 1;

	auto add = [&](IOHIDElementDescription e, uint32_t count) {
		e.wire.cookieMin = cookie;
		e.wire.cookieMax = cookie + count - 1;
		cookie += count;
		elements.push_back(e);
	};
	auto field = [&](uint32_t parent, uint32_t type, uint32_t page, uint32_t first, uint32_t last,
		int32_t min, int32_t max, uint32_t size, uint32_t offset, uint32_t flags) {
		IOHIDElementDescription e = {};
		uint32_t count = last - first + 1;

		e.wire.type = type;
		e.wire.parentCookie = parent;
		e.wire.flags = flags;
		e.wire.usagePage = page;
		e.wire.usageMin = first;
		e.wire.usageMax = last;
		e.wire.min = e.wire.scaledMin = min;
		e.wire.max = e.wire.scaledMax = max;
		e.wire.size = e.wire.reportSize = size;
		e.wire.reportCount = e.wire.rawReportCount = count;
		e.wire.bytes = e.wire.valueSize = (size + 7) / 8;
		e.bitOffset = offset;
		add(e, count);
	};

	IOHIDElementDescription collection = {};
	collection.wire.type = kIOHIDLibTypeCollection;
	collection.wire.collectionType = kIOHIDLibCollectionApplication;
	collection.wire.usagePage = 0x01;
	collection.wire.usageMin = collection.wire.usageMax = iokitd_input_primary_usage(&dev);
	add(collection, 1);

	if (dev.kind == IOKITD_INPUT_KEYBOARD)
	{
		field(1, kIOHIDLibTypeInputButton, 0x07, 0xe0, 0xe7, 0, 1, 1, 0, kIOHIDLibFlagsVariable);
		field(1, kIOHIDLibTypeInputButton, 0x07, IOKITD_KEYBOARD_FIRST_USAGE, IOKITD_KEYBOARD_LAST_USAGE, 0, 1, 1, 8,
			kIOHIDLibFlagsVariable);
		if (dev.has_leds)
			field(1, kIOHIDLibTypeOutput, 0x08, 1, IOKITD_KEYBOARD_LED_COUNT, 0, 1, 1, 0, kIOHIDLibFlagsVariable);
	}
	else
	{
		IOHIDElementDescription pointer = {};
		pointer.wire.type = kIOHIDLibTypeCollection;
		pointer.wire.collectionType = kIOHIDLibCollectionPhysical;
		pointer.wire.parentCookie = 1;
		pointer.wire.usagePage = 0x01;
		pointer.wire.usageMin = pointer.wire.usageMax = 0x01;
		add(pointer, 1);

		const uint32_t relative = kIOHIDLibFlagsVariable | kIOHIDLibFlagsRelative;

		field(2, kIOHIDLibTypeInputButton, 0x09, 1, dev.button_count, 0, 1, 1, 0, kIOHIDLibFlagsVariable);
		field(2, kIOHIDLibTypeInputMisc, 0x01, 0x30, 0x30, -32768, 32767, 16, dev.x_offset * 8, relative);
		field(2, kIOHIDLibTypeInputMisc, 0x01, 0x31, 0x31, -32768, 32767, 16, dev.y_offset * 8, relative);
		if (dev.has_wheel)
			field(2, kIOHIDLibTypeInputMisc, 0x01, 0x38, 0x38, -127, 127, 8, dev.wheel_offset * 8, relative);
		if (dev.has_hwheel)
			field(2, kIOHIDLibTypeInputMisc, 0x0c, 0x238, 0x238, -127, 127, 8, dev.hwheel_offset * 8, relative);
	}

	// The input report as a whole, for clients that want the bytes
	IOHIDElementDescription report = {};
	report.reportHandler = true;
	report.wire.type = kIOHIDLibTypeInputMisc;
	report.wire.flags = kIOHIDLibFlagsVariable;
	report.wire.size = dev.report_size * 8;
	report.wire.reportSize = 8;
	report.wire.reportCount = report.wire.rawReportCount = dev.report_size;
	report.wire.bytes = report.wire.valueSize = dev.report_size;
	add(report, 1);

	return elements;
}

IOHIDInputDevice::IOHIDInputDevice(const struct iokitd_input& dev)
: m_dev(dev), m_report(dev.report_size, 0), m_previous(dev.report_size, 0), m_leds(dev.has_leds ? 1 : 0, 0),
	m_elements(elementsOf(dev)), m_properties([propertiesOf(dev) retain])
{
}

IOHIDInputDevice::~IOHIDInputDevice()
{
	stopReading();
	[m_properties release];
}

const char* IOHIDInputDevice::className() const
{
	return "IOHIDDevice";
}

bool IOHIDInputDevice::conformsTo(const char* name)
{
	return std::strcmp(name, "IOHIDDevice") == 0 || IOService::conformsTo(name);
}

bool IOHIDInputDevice::matches(NSDictionary* dict)
{
	NSDictionary* rest;

	if (!IOHIDMatchUsagePairs(dict, m_properties, &rest))
		return false;
	return IOService::matches(rest);
}

NSDictionary* IOHIDInputDevice::matchingDictionary()
{
	NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithDictionary: m_properties];
	dict[@"IOClass"] = @"IOHIDDevice";
	return dict;
}

NSDictionary* IOHIDInputDevice::getProperties()
{
	return m_properties;
}

IOReturn IOHIDInputDevice::newUserClient(uint32_t type, IOUserClient** client)
{
	if (type != kIOHIDLibConnectTypeManager)
	{
		*client = nullptr;
		return kIOReturnUnsupported;
	}
	*client = new IOHIDLibUserClient(this);
	return kIOReturnSuccess;
}

const std::vector<uint8_t>* IOHIDInputDevice::hidOutputReport(uint32_t reportID) const
{
	return m_dev.has_leds && reportID == 0 ? &m_leds : nullptr;
}

IOReturn IOHIDInputDevice::hidSetReport(uint32_t type, uint32_t reportID, const uint8_t* data, size_t length)
{
	if (type != kIOHIDLibReportOutput || !hidOutputReport(reportID))
		return kIOReturnUnsupported;
	if (length < 1 || length > 1)
		return kIOReturnBadArgument;
	if (m_fd < 0)
		return kIOReturnNoDevice;

	// Bits 5 .. 7 are padding
	uint8_t leds = data[0] & ((1u << IOKITD_KEYBOARD_LED_COUNT) - 1);

	if (iokitd_input_set_leds(m_fd, leds) < 0)
		return (errno == EBADF || errno == EACCES || errno == EPERM) ? kIOReturnNotPermitted : kIOReturnError;
	m_leds[0] = leds;
	return kIOReturnSuccess;
}

void IOHIDInputDevice::startReading(void (^onGone)(IOHIDInputDevice*))
{
	m_fd = iokitd_input_open(m_dev.path);
	if (m_fd < 0)
	{
		onGone(this);
		return;
	}
	iokitd_input_initial_report(m_fd, &m_dev, m_report.data());
	m_previous = m_report;
	if (m_dev.has_leds)
		m_leds[0] = iokitd_input_led_state(m_fd);

	m_readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, m_fd, 0, dispatch_get_main_queue());
	dispatch_source_set_event_handler(m_readSource, ^{
		// Each frame of events is a report of its own (motion is the sum over one frame)
		int result = iokitd_input_read(m_fd, &m_dev, m_report.data(), [](const uint8_t*, void* context) {
			IOHIDInputDevice* self = static_cast<IOHIDInputDevice*>(context);
			std::vector<uint8_t> previous = self->m_previous;

			self->m_previous = self->m_report;
			self->hidReportChanged(previous);
		}, this);
		if (result < 0)
		{
			dispatch_source_cancel(m_readSource);
			onGone(this);
		}
	});
	dispatch_resume(m_readSource);
}

void IOHIDInputDevice::stopReading()
{
	if (m_readSource)
	{
		int fd = m_fd;
		dispatch_source_set_cancel_handler(m_readSource, ^{ close(fd); });
		dispatch_source_cancel(m_readSource);
		dispatch_release(m_readSource);
		m_readSource = nullptr;
		m_fd = -1;
	}
	else if (m_fd >= 0)
	{
		close(m_fd);
		m_fd = -1;
	}
}
