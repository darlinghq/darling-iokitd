/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Developers

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

#include "IOPlatformExpertDevice.h"
#import <Foundation/NSString.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#include <IOKit/IOKitKeys.h>
#include <darling/emulation/linux_premigration/ext/for-platform-identity.h>
#include <os/log.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include <unistd.h>
#include <cstring>
#include <string>

// Device tree strings are NUL-terminated CFData. Some are padded with NULs to a minimum
// length, as macOS pads serial-number to 32 bytes.
static NSData* deviceTreeString(const char* string, size_t minimumLength = 0)
{
	size_t length = strlen(string) + 1;
	NSMutableData* data = [NSMutableData dataWithBytes: string length: length];
	if (length < minimumLength)
		[data increaseLengthBy: minimumLength - length];
	return data;
}

static std::string sysctlString(const char* name)
{
	char buffer[256];
	size_t length = sizeof(buffer);
	if (sysctlbyname(name, buffer, &length, NULL, 0) != 0 || length == 0)
		return std::string();
	buffer[sizeof(buffer) - 1] = '\0';
	return buffer;
}

static NSDictionary* platformProperties()
{
	NSMutableDictionary* properties = [NSMutableDictionary dictionary];
	dserver_platform_identity_t identity;
	int error = __darling_get_platform_identity(&identity);

	if (error != 0)
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: cannot read the platform identity: %{public}s", strerror(error));
		memset(&identity, 0, sizeof(identity));
	}

	properties[@"manufacturer"] = deviceTreeString("Apple Inc.");

	// the model is what sysctl hw.model reports, as on macOS
	std::string model = sysctlString("hw.model");
	if (!model.empty())
		properties[@"model"] = deviceTreeString(model.c_str());

	// the platform UUID is what gethostuuid() reports, as on macOS
	uuid_t uuid;
	struct timespec wait = { 0, 0 };
	if (gethostuuid(uuid, &wait) == 0)
	{
		uuid_string_t text;
		uuid_unparse_upper(uuid, text);
		properties[@kIOPlatformUUIDKey] = [NSString stringWithUTF8String: text];
	}

	if (identity.serial_number[0] != '\0')
	{
		properties[@kIOPlatformSerialNumberKey] = [NSString stringWithUTF8String: identity.serial_number];
		properties[@"serial-number"] = deviceTreeString(identity.serial_number, 32);
	}

#if defined(__x86_64__) || defined(__i386__)
	// On Intel Macs, the product name (the model identifier) and the "Mac-" board ID are
	// properties of the platform expert device. Apple silicon Macs publish them elsewhere
	// and in another form, which this service does not model.
	if (identity.product_name[0] != '\0')
		properties[@"product-name"] = deviceTreeString(identity.product_name);
	if (identity.board_id[0] != '\0')
		properties[@"board-id"] = deviceTreeString(identity.board_id);
#endif

	return properties;
}

IOPlatformExpertDevice::IOPlatformExpertDevice()
{
	// read once: the identity is fixed for the lifetime of the process anyway
	m_properties = [platformProperties() retain];
}

const char* IOPlatformExpertDevice::className() const
{
	return "IOPlatformExpertDevice";
}

NSDictionary* IOPlatformExpertDevice::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOPlatformExpertDevice",
		@"IOClass": @"IOPlatformExpertDevice",
	};
}

NSDictionary* IOPlatformExpertDevice::getProperties()
{
	return m_properties;
}

void IOPlatformExpertDevice::registerSelf(ServiceRegistry* targetServiceRegistry)
{
	static IOPlatformExpertDevice* device = new IOPlatformExpertDevice;

	targetServiceRegistry->registerService(device);

	// the entry is named after the model, as the root's platform child is on Intel Macs
	NSData* model = device->m_properties[@"model"];
	std::string name = model ? (const char*) [model bytes] : "IOPlatformExpertDevice";
	device->registerInPlane(kIOServicePlane, name.c_str(), IORegistryEntry::root());
}
