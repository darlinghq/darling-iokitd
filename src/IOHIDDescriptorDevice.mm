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

#include "IOHIDDescriptorDevice.h"
#include "IOHIDHostIdentity.h"
#include "IOHIDDeviceMatching.h"
#include "HostGamepad.h"
#include "HostInput.h"
#include "IOUserClient.h"
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>
#include <IOKit/IOKitKeys.h>
#include <os/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
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
// The plug-in type of the IOHIDDeviceInterface family (kIOHIDDeviceUserClientTypeID), served by the same plug-in
static NSString* const kHIDDeviceUserClientTypeID = @"FA12FA38-6F1A-11D4-BA0C-0005028F18D5";
static NSString* const kHIDLibPlugIn = @"IOHIDFamily.kext/Contents/PlugIns/IOHIDLib.plugin";

// Where the guest sees the host's sysfs
static const char* const kHostSysfs = "/Volumes/SystemRoot/sys";
static const char* const kHostInputDirectory = "/dev/input";

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

static NSDictionary* propertiesOf(const IOHIDDescriptorSource& source, const struct iokitd_hid_descriptor& descriptor)
{
	NSMutableDictionary* props = [NSMutableDictionary dictionary];
	NSMutableArray* pairs = [NSMutableArray array];

	props[kKeyPlugInTypes] = @{ kHIDDeviceTypeID: kHIDLibPlugIn, kHIDDeviceUserClientTypeID: kHIDLibPlugIn };
	props[kKeyTransport] = transportName(source.bustype);
	props[kKeyVendorID] = @(source.vendor);
	props[kKeyProductID] = @(source.product);
	props[kKeyVersionNumber] = @(source.version);
	props[kKeyProduct] = [NSString stringWithUTF8String: source.name.empty() ? "HID Device" : source.name.c_str()];
	if (!source.serial.empty())
		props[kKeySerialNumber] = [NSString stringWithUTF8String: source.serial.c_str()];

	// The number of the event node identifies the device on this machine
	size_t slash = source.path.rfind('/');
	props[kKeyLocationID] = @(slash == std::string::npos ? 0 : std::atoi(source.path.c_str() + slash + 6));

	for (size_t i = 0; i < descriptor.pair_count; i++)
		[pairs addObject: @{ kKeyDeviceUsagePage: @(descriptor.pairs[i].page), kKeyDeviceUsage: @(descriptor.pairs[i].usage) }];
	if (descriptor.pair_count)
	{
		props[kKeyPrimaryUsagePage] = @(descriptor.pairs[0].page);
		props[kKeyPrimaryUsage] = @(descriptor.pairs[0].usage);
	}
	props[kKeyDeviceUsagePairs] = pairs;
	props[kKeyReportDescriptor] = [NSData dataWithBytes: source.descriptor.data() length: source.descriptor.size()];
	props[kKeyMaxInputReportSize] = @(iokitd_hid_max_report_bytes(&descriptor, IOKITD_HID_INPUT));
	props[kKeyMaxOutputReportSize] = @(iokitd_hid_max_report_bytes(&descriptor, IOKITD_HID_OUTPUT));
	props[kKeyMaxFeatureReportSize] = @(iokitd_hid_max_report_bytes(&descriptor, IOKITD_HID_FEATURE));
	IOHIDApplyHostIdentity(props, source.path.c_str());
	return props;
}

// The elements of the descriptor as the user client lists them (IOHIDLibUserClient.h)
static std::vector<IOHIDElementDescription> elementsOf(const struct iokitd_hid_descriptor& descriptor)
{
	std::vector<IOHIDElementDescription> elements;

	for (size_t i = 0; i < descriptor.element_count; i++)
	{
		const struct iokitd_hid_element& e = descriptor.elements[i];
		IOHIDElementDescription out = {};
		IOHIDLibElement& wire = out.wire;

		wire.cookieMin = e.cookie_min;
		wire.cookieMax = e.cookie_max;
		wire.parentCookie = e.parent;
		wire.usagePage = e.usage_page;
		wire.usageMin = e.usage_min;
		wire.usageMax = e.usage_max;

		if (e.kind == IOKITD_HID_COLLECTION)
		{
			wire.type = kIOHIDLibTypeCollection;
			wire.collectionType = e.collection_type;
			elements.push_back(out);
			continue;
		}

		wire.flags = e.flags;
		wire.reportID = e.report_id;
		wire.min = e.logical_min;
		wire.max = e.logical_max;
		wire.scaledMin = e.physical_min;
		wire.scaledMax = e.physical_max;
		wire.size = e.size;
		wire.reportSize = e.report_size;
		wire.reportCount = wire.rawReportCount = e.report_count;
		wire.unit = e.unit;
		wire.unitExponent = e.unit_exponent;
		wire.bytes = wire.valueSize = (e.size + 7) / 8;
		out.bitOffset = e.bit_offset;

		if (e.kind == IOKITD_HID_HANDLER)
		{
			out.reportHandler = true;
			wire.type = kIOHIDLibTypeInputMisc;
		}
		else if (e.kind == IOKITD_HID_OUTPUT)
			wire.type = kIOHIDLibTypeOutput;
		else if (e.kind == IOKITD_HID_FEATURE)
			wire.type = kIOHIDLibTypeFeature;
		else
			// The Keyboard/Keypad and Button pages are buttons, everything else misc
			wire.type = (e.usage_page == 0x07 || e.usage_page == 0x09) ? kIOHIDLibTypeInputButton : kIOHIDLibTypeInputMisc;

		// The fields that share one usage: the first element stands for all of them
		if (e.duplicates)
			wire.duplicateValueSize = (e.size + 7) / 8;
		if (e.array == IOKITD_HID_ARRAY_USAGES)
		{
			out.arrayUsages = true;
			out.slotBitOffset = e.slot_bit_offset;
			out.slotSize = e.slot_size;
			out.slotCount = e.slot_count;
		}
		elements.push_back(out);
	}
	return elements;
}

IOHIDDescriptorDevice::IOHIDDescriptorDevice(const IOHIDDescriptorSource& source)
: m_path(source.path), m_hasLeds(source.hasLeds), m_device(source.device)
{
	const struct iokitd_hid_descriptor* descriptor = iokitd_hid_device_descriptor(m_device);

	for (size_t i = 0; i < iokitd_hid_device_input_count(m_device); i++)
	{
		size_t bytes;
		const uint8_t* data = iokitd_hid_device_input(m_device, i, &bytes);
		m_inputs.emplace_back(data, data + bytes);
	}
	for (size_t i = 0; i < descriptor->report_count; i++)
		if (descriptor->reports[i].kind == IOKITD_HID_OUTPUT)
			m_outputs[descriptor->reports[i].id] = std::vector<uint8_t>((descriptor->reports[i].bits + 7) / 8, 0);
	m_elements = elementsOf(*descriptor);
	m_properties = [propertiesOf(source, *descriptor) retain];
}

IOHIDDescriptorDevice::~IOHIDDescriptorDevice()
{
	stopReading();
	iokitd_hid_device_free(m_device);
	[m_properties release];
}

const char* IOHIDDescriptorDevice::className() const
{
	return "IOHIDDevice";
}

bool IOHIDDescriptorDevice::conformsTo(const char* name)
{
	return std::strcmp(name, "IOHIDDevice") == 0 || IOService::conformsTo(name);
}

bool IOHIDDescriptorDevice::matches(NSDictionary* dict)
{
	NSDictionary* rest;

	if (!IOHIDMatchUsagePairs(dict, m_properties, &rest))
		return false;
	return IOService::matches(rest);
}

NSDictionary* IOHIDDescriptorDevice::matchingDictionary()
{
	NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithDictionary: m_properties];
	dict[@"IOClass"] = @"IOHIDDevice";
	return dict;
}

NSDictionary* IOHIDDescriptorDevice::getProperties()
{
	return m_properties;
}

IOReturn IOHIDDescriptorDevice::newUserClient(uint32_t type, IOUserClient** client)
{
	if (type != kIOHIDLibConnectTypeManager)
	{
		*client = nullptr;
		return kIOReturnUnsupported;
	}
	*client = new IOHIDLibUserClient(this);
	return kIOReturnSuccess;
}

const std::vector<uint8_t>& IOHIDDescriptorDevice::hidReport() const
{
	static const std::vector<uint8_t> none;

	return m_inputs.empty() ? none : m_inputs[0];
}

uint32_t IOHIDDescriptorDevice::hidInputReportID() const
{
	return m_inputs.empty() ? 0 : iokitd_hid_device_input_id(m_device, 0);
}

const std::vector<uint8_t>* IOHIDDescriptorDevice::hidInputReport(uint32_t reportID) const
{
	for (size_t i = 0; i < m_inputs.size(); i++)
		if (iokitd_hid_device_input_id(m_device, i) == reportID)
			return &m_inputs[i];
	return nullptr;
}

size_t IOHIDDescriptorDevice::hidMaxInputReportSize() const
{
	size_t longest = 0;

	for (const auto& report : m_inputs)
		longest = std::max(longest, report.size());
	return longest;
}

const std::vector<uint8_t>* IOHIDDescriptorDevice::hidOutputReport(uint32_t reportID) const
{
	auto it = m_outputs.find(reportID);
	return it == m_outputs.end() ? nullptr : &it->second;
}

IOReturn IOHIDDescriptorDevice::hidSetReport(uint32_t type, uint32_t reportID, const uint8_t* data, size_t length)
{
	uint8_t leds;
	auto it = m_outputs.find(reportID);

	// Only what the node can do is done: the LEDs of a keyboard. Another output report would need the
	// device's own interface; so would a feature report
	if (type != kIOHIDLibReportOutput || it == m_outputs.end())
		return kIOReturnUnsupported;
	if (!iokitd_hid_output_leds(iokitd_hid_device_descriptor(m_device), reportID, data, length, &leds) || !m_hasLeds)
		return kIOReturnUnsupported;
	if (length == 0 || length > it->second.size())
		return kIOReturnBadArgument;
	if (m_fd < 0)
		return kIOReturnNoDevice;
	if (iokitd_input_set_leds(m_fd, leds) < 0)
		return (errno == EBADF || errno == EACCES || errno == EPERM) ? kIOReturnNotPermitted : kIOReturnError;

	it->second.assign(it->second.size(), 0);
	std::copy(data, data + length, it->second.begin());
	return kIOReturnSuccess;
}

void IOHIDDescriptorDevice::startReading(void (^onGone)(IOHIDDescriptorDevice*))
{
	m_fd = iokitd_input_open(m_path.c_str());
	if (m_fd < 0)
	{
		onGone(this);
		return;
	}
	iokitd_hid_device_initial(m_device, m_fd);
	for (size_t i = 0; i < m_inputs.size(); i++)
	{
		size_t bytes;
		const uint8_t* data = iokitd_hid_device_input(m_device, i, &bytes);
		m_inputs[i].assign(data, data + bytes);
	}

	m_readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, m_fd, 0, dispatch_get_main_queue());
	dispatch_source_set_event_handler(m_readSource, ^{
		// Each frame of events changes the reports it changes: told one by one
		int result = iokitd_hid_device_read(m_device, m_fd, [](size_t index, uint32_t id, const uint8_t* previous,
			const uint8_t* current, size_t bytes, void* context) {
			IOHIDDescriptorDevice* self = static_cast<IOHIDDescriptorDevice*>(context);
			std::vector<uint8_t> before = self->m_inputs[index];

			self->m_inputs[index].assign(current, current + bytes);
			self->hidReportChanged(id, before, self->m_inputs[index]);
		}, this);
		if (result < 0)
		{
			dispatch_source_cancel(m_readSource);
			onGone(this);
		}
	});
	dispatch_resume(m_readSource);
}

void IOHIDDescriptorDevice::stopReading()
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

bool IOHIDDescriptorDevice::probe(const std::string& path, const std::string& name, const std::string& serial,
	uint16_t bustype, uint16_t vendor, uint16_t product, uint16_t version, bool rumble, bool leds, IOHIDDescriptorSource* source)
{
	size_t slash = path.rfind('/');
	std::string event = slash == std::string::npos ? path : path.substr(slash + 1);
	uint8_t* descriptor = nullptr;
	size_t size = 0;
	char hidDir[1024];
	char siblings[16][32];

	// The output reports of a controller with force feedback are the ones that make it rumble, which
	// the host does through the node: that is described by IOHIDGamepadDevice
	if (rumble)
		return false;
	if (iokitd_hid_sysfs_descriptor(kHostSysfs, event.c_str(), &descriptor, &size, hidDir, sizeof(hidDir)) < 0)
		return false;

	// The reports of a device that has several nodes (a keyboard and its media keys, a pad and its
	// motion sensor) would be shared by the services of those nodes: only a device with one node of its
	// kind is described by its descriptor
	int count = iokitd_hid_sysfs_siblings(hidDir, siblings, 16);
	for (int i = 0; i < count; i++)
	{
		if (event == siblings[i])
			continue;

		std::string other = std::string(kHostInputDirectory) + "/" + siblings[i];
		struct iokitd_gamepad pad;
		struct iokitd_input input;

		if (iokitd_gamepad_probe(other.c_str(), &pad) == 0 || iokitd_input_probe(other.c_str(), &input) == 0)
		{
			free(descriptor);
			return false;
		}
	}

	struct iokitd_hid_caps caps;
	int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0 || iokitd_hid_caps_read(fd, &caps) < 0)
	{
		if (fd >= 0)
			close(fd);
		free(descriptor);
		return false;
	}
	close(fd);

	struct iokitd_hid_device* device = iokitd_hid_device_create(descriptor, size, &caps);
	if (!device)
	{
		free(descriptor);
		return false;
	}

	source->path = path;
	source->name = name;
	source->serial = serial;
	source->bustype = bustype;
	source->vendor = vendor;
	source->product = product;
	source->version = version;
	source->hasLeds = leds;
	source->descriptor.assign(descriptor, descriptor + size);
	source->device = device;
	free(descriptor);
	return true;
}
