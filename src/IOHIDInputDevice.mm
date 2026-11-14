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
	props[kKeyDeviceUsagePairs] = @[ @{ kKeyDeviceUsagePage: @1, kKeyDeviceUsage: @(iokitd_input_primary_usage(&dev)) } ];
	if (length > 0)
		props[kKeyReportDescriptor] = [NSData dataWithBytes: descriptor length: length];
	props[kKeyMaxInputReportSize] = @(dev.report_size);
	props[kKeyMaxOutputReportSize] = @0;
	props[kKeyMaxFeatureReportSize] = @0;
	return props;
}

IOHIDInputDevice::IOHIDInputDevice(const struct iokitd_input& dev)
: m_dev(dev), m_report(dev.report_size, 0), m_properties([propertiesOf(dev) retain])
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

void IOHIDInputDevice::startReading(void (^onGone)(IOHIDInputDevice*))
{
	m_fd = iokitd_input_open(m_dev.path);
	if (m_fd < 0)
	{
		onGone(this);
		return;
	}
	iokitd_input_initial_report(m_fd, &m_dev, m_report.data());

	m_readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, m_fd, 0, dispatch_get_main_queue());
	dispatch_source_set_event_handler(m_readSource, ^{
		int result = iokitd_input_read(m_fd, &m_dev, m_report.data(), nullptr, nullptr);
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
