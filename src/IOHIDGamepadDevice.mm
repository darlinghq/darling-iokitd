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

#include "IOHIDGamepadDevice.h"
#include "IOHIDDeviceMatching.h"
#include "IOUserClient.h"
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>
#include <IOKit/IOKitKeys.h>
#include <os/log.h>
#include <unistd.h>
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
	// BUS_USB, BUS_BLUETOOTH
	switch (bustype)
	{
		case 0x03: return @"USB";
		case 0x05: return @"Bluetooth";
		default: return @"Virtual";
	}
}

static NSDictionary* propertiesOf(const struct iokitd_gamepad& pad)
{
	uint8_t descriptor[1024];
	int length = iokitd_gamepad_report_descriptor(&pad, descriptor, sizeof(descriptor));
	NSMutableDictionary* props = [NSMutableDictionary dictionary];

	props[kKeyPlugInTypes] = @{ kHIDDeviceTypeID: kHIDLibPlugIn };
	props[kKeyTransport] = transportName(pad.bustype);
	props[kKeyVendorID] = @(pad.vendor);
	props[kKeyProductID] = @(pad.product);
	props[kKeyVersionNumber] = @(pad.version);
	props[kKeyProduct] = [NSString stringWithUTF8String: pad.name[0] ? pad.name : "Game Controller"];
	if (pad.serial[0])
		props[kKeySerialNumber] = [NSString stringWithUTF8String: pad.serial];

	// The number of the event node identifies the controller on this machine
	const char* base = std::strrchr(pad.path, '/');
	props[kKeyLocationID] = @(base ? std::atoi(base + 6) : 0);

	props[kKeyPrimaryUsagePage] = @1;
	props[kKeyPrimaryUsage] = @(iokitd_gamepad_primary_usage(&pad));
	props[kKeyDeviceUsagePairs] = @[ @{ kKeyDeviceUsagePage: @1, kKeyDeviceUsage: @(iokitd_gamepad_primary_usage(&pad)) } ];
	if (length > 0)
		props[kKeyReportDescriptor] = [NSData dataWithBytes: descriptor length: length];
	props[kKeyMaxInputReportSize] = @(pad.report_size);
	props[kKeyMaxOutputReportSize] = @0;
	props[kKeyMaxFeatureReportSize] = @0;
	return props;
}

// The elements of the report descriptor iokitd generates for the controller
// (HostGamepad.h): one application collection holding the items of the
// report in descriptor order (the buttons as one struct of consecutive
// usages, the hat, the axes), and the handler of the input report. Cookies run from 1 in that order.
static std::vector<IOHIDElementDescription> elementsOf(const struct iokitd_gamepad& pad)
{
	std::vector<IOHIDElementDescription> elements;
	uint32_t cookie = 1;

	auto add = [&](IOHIDElementDescription e, uint32_t count) {
		e.wire.cookieMin = cookie;
		e.wire.cookieMax = cookie + count - 1;
		cookie += count;
		elements.push_back(e);
	};

	IOHIDElementDescription collection = {};
	collection.wire.type = kIOHIDLibTypeCollection;
	collection.wire.collectionType = kIOHIDLibCollectionApplication;
	collection.wire.usagePage = 0x01;
	collection.wire.usageMin = collection.wire.usageMax = iokitd_gamepad_primary_usage(&pad);
	add(collection, 1);

	for (int i = 0; i < pad.field_count; i++)
	{
		const struct iokitd_gamepad_field& f = pad.fields[i];
		IOHIDElementDescription e = {};

		e.wire.parentCookie = 1;
		e.wire.flags = kIOHIDLibFlagsVariable;
		e.wire.reportCount = e.wire.rawReportCount = 1;
		e.bitOffset = f.bit_offset;

		if (f.kind == IOKITD_GAMEPAD_FIELD_BUTTONS)
		{
			// Usages 1 .. button_bits, one element each, one bit each
			e.wire.type = kIOHIDLibTypeInputButton;
			e.wire.usagePage = 0x09;
			e.wire.usageMin = 1;
			e.wire.usageMax = pad.button_bits;
			e.wire.min = e.wire.scaledMin = 0;
			e.wire.max = e.wire.scaledMax = 1;
			e.wire.size = e.wire.reportSize = 1;
			e.wire.reportCount = e.wire.rawReportCount = pad.button_bits;
			e.wire.bytes = e.wire.valueSize = 1;
			add(e, pad.button_bits);
		}
		else if (f.kind == IOKITD_GAMEPAD_FIELD_HAT)
		{
			e.wire.type = kIOHIDLibTypeInputMisc;
			e.wire.flags |= kIOHIDLibFlagsNullState;
			e.wire.usagePage = 0x01;
			e.wire.usageMin = e.wire.usageMax = 0x39;
			e.wire.min = pad.hat_logical_min;
			e.wire.max = pad.hat_logical_min + 7;
			e.wire.scaledMin = 0;
			e.wire.scaledMax = 315;
			e.wire.size = e.wire.reportSize = 4;
			e.wire.unit = 0x14; // degrees
			e.wire.bytes = e.wire.valueSize = 1;
			add(e, 1);
		}
		else
		{
			const struct iokitd_gamepad_axis& axis = pad.axes[f.index];

			e.wire.type = kIOHIDLibTypeInputMisc;
			e.wire.usagePage = axis.usage_page;
			e.wire.usageMin = e.wire.usageMax = axis.usage;
			e.wire.min = e.wire.scaledMin = 0;
			e.wire.max = e.wire.scaledMax = axis.maximum - axis.minimum;
			e.wire.size = e.wire.reportSize = axis.bits;
			e.wire.bytes = e.wire.valueSize = (axis.bits + 7) / 8;
			add(e, 1);
		}
	}

	// The input report as a whole, for clients that want the bytes
	IOHIDElementDescription report = {};
	report.reportHandler = true;
	report.wire.type = kIOHIDLibTypeInputMisc;
	report.wire.flags = kIOHIDLibFlagsVariable;
	report.wire.size = pad.report_size * 8;
	report.wire.reportSize = 8;
	report.wire.reportCount = report.wire.rawReportCount = pad.report_size;
	report.wire.bytes = report.wire.valueSize = pad.report_size;
	add(report, 1);

	return elements;
}

IOHIDGamepadDevice::IOHIDGamepadDevice(const struct iokitd_gamepad& pad)
: m_pad(pad), m_report(pad.report_size, 0), m_elements(elementsOf(pad)), m_properties([propertiesOf(pad) retain])
{
}

IOHIDGamepadDevice::~IOHIDGamepadDevice()
{
	stopReading();
	[m_properties release];
}

const char* IOHIDGamepadDevice::className() const
{
	return "IOHIDDevice";
}

bool IOHIDGamepadDevice::conformsTo(const char* name)
{
	return std::strcmp(name, "IOHIDDevice") == 0 || IOService::conformsTo(name);
}

bool IOHIDGamepadDevice::matches(NSDictionary* dict)
{
	NSDictionary* rest;

	if (!IOHIDMatchUsagePairs(dict, m_properties, &rest))
		return false;
	return IOService::matches(rest);
}

NSDictionary* IOHIDGamepadDevice::matchingDictionary()
{
	NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithDictionary: m_properties];
	dict[@"IOClass"] = @"IOHIDDevice";
	return dict;
}

NSDictionary* IOHIDGamepadDevice::getProperties()
{
	return m_properties;
}

IOReturn IOHIDGamepadDevice::newUserClient(uint32_t type, IOUserClient** client)
{
	if (type != kIOHIDLibConnectTypeManager)
	{
		*client = nullptr;
		return kIOReturnUnsupported;
	}
	*client = new IOHIDLibUserClient(this);
	return kIOReturnSuccess;
}

void IOHIDGamepadDevice::startReading(void (^onGone)(IOHIDGamepadDevice*))
{
	m_fd = iokitd_gamepad_open(m_pad.path);
	if (m_fd < 0)
	{
		onGone(this);
		return;
	}
	iokitd_gamepad_initial_report(m_fd, &m_pad, m_report.data());

	m_readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, m_fd, 0, dispatch_get_main_queue());
	dispatch_source_set_event_handler(m_readSource, ^{
		std::vector<uint8_t> previous = m_report;
		int result = iokitd_gamepad_read(m_fd, &m_pad, m_report.data());
		if (result < 0)
		{
			dispatch_source_cancel(m_readSource);
			onGone(this);
		}
		else if (result > 0)
			hidReportChanged(previous);
	});
	dispatch_resume(m_readSource);
}

void IOHIDGamepadDevice::stopReading()
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
