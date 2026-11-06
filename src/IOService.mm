/*
 This file is part of Darling.

 Copyright (C) 2020 Lubos Dolezel

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

#include "IOService.h"
#include "IOUserClient.h"
#include "IOInterestNotification.h"
#include <algorithm>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

extern "C" {
#include "iokitmigServer.h"
}

IOService::IOService()
{

}

// https://unix.superglobalmegacorp.com/xnu/newsrc/iokit/KernelConfigTables.cpp.html
bool IOService::matches(NSDictionary* dict)
{
	NSDictionary* ourProps = matchingDictionary();

	for (NSString* key in dict)
	{
		NSObject* ourProp = ourProps[key];
		NSObject* expectedValue = dict[key];

		// In a matching dictionary, IOProviderClass names a class the service
		// must be an instance of (or inherit from). In our personality it names
		// the provider we attach to, so the two can't be compared directly.
		if ([key isEqualToString: @"IOProviderClass"])
		{
			if (![expectedValue isKindOfClass:[NSString class]] || !conformsTo([(NSString*) expectedValue UTF8String]))
				return false;
			continue;
		}

		if ([ourProp isKindOfClass:[NSArray class]])
		{
			NSArray* array = (NSArray*) ourProp;
			if (![array containsObject: expectedValue])
				return false;
		}
		else if (ourProp != nil)
		{
			if (![expectedValue isEqual: ourProp])
				return false;
		}
		else
		{
			if ([key isEqualToString: @"IONameMatch"])
			{
				if (![ourProps[@"IOClass"] isEqual: expectedValue])
					return false;
			}
			else
				return false;
		}
	}
	return true;
}

bool IOService::conformsTo(const char* className)
{
	if (strcmp(className, "IOService") == 0)
		return true;
	return IOObject::conformsTo(className);
}

IOReturn IOService::newUserClient(uint32_t type, IOUserClient** client)
{
	*client = nullptr;
	return kIOReturnUnsupported;
}

bool IOService::acceptsInterest(const char* type)
{
	return false;
}

void IOService::addInterest(IOInterestNotification* notification)
{
	m_interests.push_back(notification);
}

void IOService::removeInterest(IOInterestNotification* notification)
{
	auto it = std::find(m_interests.begin(), m_interests.end(), notification);
	if (it == m_interests.end())
		return;

	m_interests.erase(it);
	interestRemoved(notification);
}

std::vector<IOInterestNotification*> IOService::interests(const char* type) const
{
	std::vector<IOInterestNotification*> result;
	for (IOInterestNotification* notification : m_interests)
	{
		if (notification->interestType() == type)
			result.push_back(notification);
	}
	return result;
}

kern_return_t is_io_service_open_extended
(
	mach_port_t service,
	task_t owningTask,
	uint32_t connect_type,
	NDR_record_t ndr,
	io_buf_ptr_t properties,
	mach_msg_type_number_t propertiesCnt,
	kern_return_t *result,
	mach_port_t *connection
)
{
	*connection = MACH_PORT_NULL;

	// The task right and the out-of-line properties in the request are
	// ours once we return KERN_SUCCESS.
	if (properties)
		vm_deallocate(mach_task_self(), (vm_address_t) properties, propertiesCnt);
	if (MACH_PORT_VALID(owningTask))
		mach_port_deallocate(mach_task_self(), owningTask);

	IOService* s = dynamic_cast<IOService*>(IOObject::lookup(service));
	if (!s)
	{
		*result = kIOReturnBadArgument;
		return KERN_SUCCESS;
	}

	IOUserClient* client = nullptr;
	*result = s->newUserClient(connect_type, &client);

	if (*result == kIOReturnSuccess && client)
	{
		*connection = client->port();
		// The reply carries a send right of its own; drop ours afterwards.
		client->releaseLater();
	}
	else if (*result == kIOReturnSuccess)
		*result = kIOReturnUnsupported;

	return KERN_SUCCESS;
}

kern_return_t is_io_service_close
(
	mach_port_t connection
)
{
	// The connection goes away when the caller releases its last
	// reference, which IOServiceClose() does right after this call.
	if (!dynamic_cast<IOUserClient*>(IOObject::lookup(connection)))
		return kIOReturnBadArgument;
	return kIOReturnSuccess;
}

kern_return_t is_io_service_get_state
(
	mach_port_t service,
	uint64_t *state,
	uint32_t *busy_state,
	uint64_t *accumulated_busy_time
)
{
    IOService* e = dynamic_cast<IOService*>(IOObject::lookup(service));
	if (!e)
		return kIOReturnBadArgument;

	*state = e->state();
	*busy_state = e->busyState();
	*accumulated_busy_time = e->accumulatedBusyTime();

    return kIOReturnSuccess;
}
