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

#include "iokitd.h"
#include "ServiceRegistry.h"
#include <IOKit/IOCFUnserialize.h>
#include <CoreFoundation/CFString.h>
#include <IOKit/IOReturn.h>
#include <os/log.h>
#include <stdexcept>
#include <cstring>
#include <vector>
#include "IOIterator.h"

extern "C" {
#include "iokitmigServer.h"
}

ServiceRegistry* ServiceRegistry::instance()
{
	static ServiceRegistry reg;
	return &reg;
}

IOIterator* ServiceRegistry::iteratorForMatchingServices(NSDictionary* criteria) const
{
	std::vector<IOObject*> matching;

	for (auto it = m_registeredServices.begin(); it != m_registeredServices.end(); it++)
	{
		if ((*it)->matches(criteria))
			matching.push_back(*it);
	}

	return new IOIterator(matching);
}

IOService* ServiceRegistry::firstMatchingService(NSDictionary* criteria) const
{
	for (IOService* service : m_registeredServices)
	{
		if (service->matches(criteria))
			return service;
	}
	return nullptr;
}

void ServiceRegistry::registerService(IOService* service)
{
	m_registeredServices.push_back(service);
}

kern_return_t is_io_service_get_matching_services_ool
(
	mach_port_t master_port,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	kern_return_t *result,
	mach_port_t *existing
)
{
	// TODO
    return kIOReturnUnsupported;
}

// Unserializes the binary matching dictionary of a request. The caller
// releases the result.
static NSDictionary* unserializeMatching(io_struct_inband_t matching, mach_msg_type_number_t matchingCnt)
{
	CFStringRef errorString = nullptr;
	CFTypeRef criteria = IOCFUnserializeBinary(matching, matchingCnt, nullptr, 0, &errorString);

	if (!criteria)
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: cannot parse a matching dictionary");
		if (errorString)
			CFRelease(errorString);
		return nil;
	}

	if (CFGetTypeID(criteria) != CFDictionaryGetTypeID())
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: the matching criteria are not a dictionary");
		CFRelease(criteria);
		return nil;
	}

	return (NSDictionary*) criteria;
}

kern_return_t is_io_service_get_matching_services_bin
(
	mach_port_t master_port,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t *existing
)
{
	*existing = MACH_PORT_NULL;

	// Criteria example:
	// IOProviderClass -> IODisplayConnect
	NSDictionary* criteria = unserializeMatching(matching, matchingCnt);
	if (!criteria)
		return kIOReturnBadArgument;

	IOIterator* iterator = ServiceRegistry::instance()->iteratorForMatchingServices(criteria);
	CFRelease(criteria);

	*existing = iterator->port();
	iterator->releaseLater();

	return kIOReturnSuccess;
}

kern_return_t is_io_service_get_matching_service_bin
(
	mach_port_t master_port,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t *service
)
{
	*service = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatching(matching, matchingCnt);
	if (!criteria)
		return kIOReturnBadArgument;

	IOService* match = ServiceRegistry::instance()->firstMatchingService(criteria);
	CFRelease(criteria);

	if (!match)
		return kIOReturnNotFound;

	*service = match->port();
	return kIOReturnSuccess;
}

kern_return_t is_io_service_add_notification_bin_64
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
	*notification = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatching(matching, matchingCnt);
	if (!criteria)
		return kIOReturnBadArgument;

	// Services are only registered while iokitd starts, so none is ever
	// published or terminated later. The notification is an iterator over
	// the services that match now, which is what the client walks to arm a
	// publish or match notification; termination notifications start empty.
	IOIterator* iterator;
	if (!strcmp(notification_type, kIOPublishNotification) ||
		!strcmp(notification_type, kIOFirstPublishNotification) ||
		!strcmp(notification_type, kIOMatchedNotification) ||
		!strcmp(notification_type, kIOFirstMatchNotification))
		iterator = ServiceRegistry::instance()->iteratorForMatchingServices(criteria);
	else
		iterator = new IOIterator(std::vector<IOObject*>());
	CFRelease(criteria);

	// Nothing is ever sent to the wake port
	if (MACH_PORT_VALID(wake_port))
		mach_port_deallocate(mach_task_self(), wake_port);

	*notification = iterator->port();
	iterator->releaseLater();

	return kIOReturnSuccess;
}

kern_return_t is_io_service_get_matching_services
(
	mach_port_t master_port,
	io_string_t matching,
	mach_port_t *existing
)
{
    // Old unsupported API
    return kIOReturnUnsupported;
}
