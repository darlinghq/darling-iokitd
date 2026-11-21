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
#include "IOServiceNotification.h"
#include "IOInterestNotification.h"
#include <IOKit/IOMessage.h>
#include <IOKit/IOKitKeys.h>
#include "NotificationRequests.h"
#include <algorithm>

extern "C" {
#include "iokitmigServer.h"
}

ServiceRegistry* ServiceRegistry::instance()
{
	static ServiceRegistry reg;
	return &reg;
}

std::vector<IOObject*> ServiceRegistry::matchingServices(NSDictionary* criteria) const
{
	std::vector<IOObject*> matching;

	for (auto it = m_registeredServices.begin(); it != m_registeredServices.end(); it++)
	{
		if ((*it)->matches(criteria))
			matching.push_back(*it);
	}

	return matching;
}

IOIterator* ServiceRegistry::iteratorForMatchingServices(NSDictionary* criteria) const
{
	return new IOIterator(matchingServices(criteria));
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

	// A notification may be destroyed by its client at any time, but only on
	// this queue, so the list cannot change while it is walked
	for (IOServiceNotification* notification : m_notifications)
	{
		if (notification->kind() != IOServiceNotification::Terminate)
			notification->serviceEvent(service);
	}
}

void ServiceRegistry::unregisterService(IOService* service)
{
	auto it = std::find(m_registeredServices.begin(), m_registeredServices.end(), service);
	if (it == m_registeredServices.end())
		return;
	m_registeredServices.erase(it);

	// What the clients that watch the service are told when it goes away
	for (IOInterestNotification* interest : service->interests(kIOGeneralInterest))
		interest->deliver(kIOMessageServiceIsTerminated, 0);

	for (IOServiceNotification* notification : m_notifications)
	{
		if (notification->kind() == IOServiceNotification::Terminate)
			notification->serviceEvent(service);
		else
			notification->remove(service);
	}
}

void ServiceRegistry::addNotification(IOServiceNotification* notification)
{
	m_notifications.push_back(notification);
}

void ServiceRegistry::removeNotification(IOServiceNotification* notification)
{
	m_notifications.remove(notification);
}

// Unserializes the binary matching dictionary of a request. The caller
// releases the result.
static NSDictionary* unserializeMatching(const char* matching, mach_msg_type_number_t matchingCnt)
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

// Unserializes the matching dictionary of an out-of-line request: the binary form, or else the XML
// form. The XML parser reads up to a NUL, so a buffer that does not end in one is refused instead of
// being read past its end. The caller releases the result.
static NSDictionary* unserializeMatchingOutOfLine(const char* matching, mach_msg_type_number_t matchingCnt)
{
	CFStringRef errorString = nullptr;
	CFTypeRef criteria = nullptr;

	if (matching == nullptr || matchingCnt == 0)
		return nil;

	criteria = IOCFUnserializeBinary(matching, matchingCnt, nullptr, 0, &errorString);
	if (!criteria)
	{
		if (errorString)
			CFRelease(errorString);
		errorString = nullptr;

		if (matching[matchingCnt - 1] != '\0')
		{
			os_log_error(OS_LOG_DEFAULT, "iokitd: an out-of-line matching dictionary is not NUL terminated");
			return nil;
		}
		criteria = IOCFUnserialize(matching, nullptr, 0, &errorString);
	}

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

// The out-of-line buffer of a request is mapped into this task for the request and is ours to free
// once the routine returns success; the outcome of a well formed request is in *result.
static void releaseOutOfLine(io_buf_ptr_t buffer, mach_msg_type_number_t size)
{
	if (buffer)
		vm_deallocate(mach_task_self(), (vm_address_t) buffer, size);
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
	*existing = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatchingOutOfLine(matching, matchingCnt);
	releaseOutOfLine(matching, matchingCnt);
	if (!criteria)
	{
		*result = kIOReturnBadArgument;
		return KERN_SUCCESS;
	}

	IOIterator* iterator = ServiceRegistry::instance()->iteratorForMatchingServices(criteria);
	CFRelease(criteria);

	*existing = iterator->port();
	iterator->releaseLater();

	*result = kIOReturnSuccess;
	return KERN_SUCCESS;
}

kern_return_t is_io_service_get_matching_service_ool
(
	mach_port_t master_port,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	kern_return_t *result,
	mach_port_t *service
)
{
	*service = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatchingOutOfLine(matching, matchingCnt);
	releaseOutOfLine(matching, matchingCnt);
	if (!criteria)
	{
		*result = kIOReturnBadArgument;
		return KERN_SUCCESS;
	}

	IOService* match = ServiceRegistry::instance()->firstMatchingService(criteria);
	CFRelease(criteria);

	if (!match)
	{
		*result = kIOReturnNotFound;
		return KERN_SUCCESS;
	}

	*service = match->port();
	*result = kIOReturnSuccess;
	return KERN_SUCCESS;
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

// Takes over the criteria (on success) and the wake port right (on success).
static kern_return_t addServiceNotification
(
	const char* notification_type,
	NSDictionary* criteria,
	mach_port_t wake_port,
	const void* reference,
	size_t referenceSize,
	bool client64,
	mach_port_t *notification
)
{
	// The notification is an iterator over the services that match: the ones
	// registered now (for publish and match notifications, which is what the
	// client walks to arm it), and the ones that come or go later, announced
	// on the wake port. Termination notifications start empty.
	IOServiceNotification::Kind kind;
	if (!strcmp(notification_type, kIOPublishNotification) ||
		!strcmp(notification_type, kIOFirstPublishNotification))
		kind = IOServiceNotification::Publish;
	else if (!strcmp(notification_type, kIOMatchedNotification) ||
		!strcmp(notification_type, kIOFirstMatchNotification))
		kind = IOServiceNotification::Match;
	else
		kind = IOServiceNotification::Terminate;

	// Takes over the criteria and the wake port right
	IOServiceNotification* iterator = new IOServiceNotification(kind, criteria, wake_port, reference,
		referenceSize, client64);

	if (kind != IOServiceNotification::Terminate)
	{
		for (IOObject* service : ServiceRegistry::instance()->matchingServices(criteria))
			iterator->append(service);
	}

	*notification = iterator->port();
	iterator->releaseLater();

	return kIOReturnSuccess;
}

kern_return_t iokitAddServiceNotification
(
	const char* notification_type,
	const char* matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	const void* reference,
	size_t referenceSize,
	bool client64,
	mach_port_t *notification
)
{
	*notification = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatching(matching, matchingCnt);
	if (!criteria)
		return kIOReturnBadArgument;

	return addServiceNotification(notification_type, criteria, wake_port, reference, referenceSize,
		client64, notification);
}

// The out-of-line forms of IOServiceAddMatchingNotification, which IOKit uses for a matching dictionary too
// large for the in-line request. The routine returns success once the request was well formed and the
// outcome is in *result; the wake port right is ours then, so a failure drops it.
kern_return_t iokitAddServiceNotificationOutOfLine
(
	const char* notification_type,
	char* matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	const void* reference,
	size_t referenceSize,
	bool client64,
	kern_return_t *result,
	mach_port_t *notification
)
{
	*notification = MACH_PORT_NULL;

	NSDictionary* criteria = unserializeMatchingOutOfLine(matching, matchingCnt);
	releaseOutOfLine(matching, matchingCnt);

	if (criteria)
		*result = addServiceNotification(notification_type, criteria, wake_port, reference, referenceSize,
			client64, notification);
	else
		*result = kIOReturnBadArgument;

	if (*result != kIOReturnSuccess && MACH_PORT_VALID(wake_port))
		mach_port_deallocate(mach_task_self(), wake_port);
	return KERN_SUCCESS;
}

kern_return_t is_io_service_add_notification_ool_64
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	kern_return_t *result,
	mach_port_t *notification
)
{
	// The MIG stub has refused a reference longer than the array before this runs.
	return iokitAddServiceNotificationOutOfLine(notification_type, matching, matchingCnt, wake_port,
		reference, referenceCnt * sizeof(io_user_reference_t), true, result, notification);
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
	if (referenceCnt > kOSAsyncRef64Count)
	{
		*notification = MACH_PORT_NULL;
		return kIOReturnBadArgument;
	}

	return iokitAddServiceNotification(notification_type, matching, matchingCnt, wake_port,
		reference, referenceCnt * sizeof(io_user_reference_t), true, notification);
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
