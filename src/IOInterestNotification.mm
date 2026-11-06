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

#include "IOInterestNotification.h"
#include "IOService.h"
#include "iokitd.h"
#include <IOKit/IOReturn.h>
#include <os/log.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

extern "C" {
#include "iokitmigServer.h"
}

// The message layout IODispatchCalloutFromMessage() expects: the provider
// as a port descriptor, followed by the notification header, the client's
// async reference and the message content.
struct InterestMessageHead
{
	mach_msg_header_t header;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t provider;
	mach_msg_size_t size;
	natural_t type;
};

// The largest message argument size, reported in the header's size field.
static const mach_msg_size_t kMaxMessageArgumentSize = 64;

// IODispatchCalloutFromMessage() finds the notification header right after the descriptors.
static_assert(offsetof(InterestMessageHead, size) == sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t)
	+ sizeof(mach_msg_port_descriptor_t), "notification header must follow the port descriptor");
static_assert(offsetof(OSNotificationHeader64, reference) == 2 * sizeof(natural_t), "unexpected header layout");
static_assert(sizeof(IOServiceInterestContent64) == sizeof(natural_t) + sizeof(io_user_reference_t),
	"unexpected interest content layout");

IOInterestNotification::IOInterestNotification(IOService* service, const char* interestType,
	mach_port_t wakePort, const void* reference, size_t referenceSize, bool client64)
: m_service(service), m_interestType(interestType), m_wakePort(wakePort),
	m_referenceSize(std::min(referenceSize, sizeof(m_reference))), m_client64(client64)
{
	std::memset(m_reference, 0, sizeof(m_reference));
	std::memcpy(m_reference, reference, m_referenceSize);
	m_service->addInterest(this);
}

IOInterestNotification::~IOInterestNotification()
{
	m_service->removeInterest(this);
	if (MACH_PORT_VALID(m_wakePort))
		mach_port_deallocate(mach_task_self(), m_wakePort);
}

const char* IOInterestNotification::className() const
{
	return "IOServiceMessageUserNotification";
}

kern_return_t IOInterestNotification::sendMessage(uint32_t messageType, uint64_t argument)
{
	// Header, reference (at most OSAsyncReference64) and IOServiceInterestContent64
	alignas(8) uint8_t buffer[sizeof(InterestMessageHead) + sizeof(OSAsyncReference64)
		+ sizeof(IOServiceInterestContent64)];
	std::memset(buffer, 0, sizeof(buffer));

	InterestMessageHead* head = reinterpret_cast<InterestMessageHead*>(buffer);
	head->header.msgh_bits = MACH_MSGH_BITS_COMPLEX
		| MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
	head->header.msgh_remote_port = m_wakePort;
	// Received as msgh_remote_port: the client checks that it still holds this notification.
	head->header.msgh_local_port = port();
	head->header.msgh_id = kOSNotificationMessageID;

	head->body.msgh_descriptor_count = 1;
	head->provider.name = m_service->port();
	head->provider.disposition = MACH_MSG_TYPE_MAKE_SEND;
	head->provider.type = MACH_MSG_PORT_DESCRIPTOR;

	// A pointer-sized argument: 8 bytes for 64-bit clients, 4 otherwise.
	// The low bits of the type would carry (size & 3), which is 0 for both.
	size_t argumentSize = m_client64 ? sizeof(io_user_reference_t) : sizeof(uint32_t);
	head->size = kMaxMessageArgumentSize + sizeof(IOServiceInterestContent64);
	head->type = kIOServiceMessageNotificationType;

	uint8_t* p = buffer + sizeof(InterestMessageHead);
	std::memcpy(p, m_reference, m_referenceSize);
	p += m_referenceSize;

	natural_t type = messageType;
	std::memcpy(p, &type, sizeof(type));
	p += sizeof(type);
	if (m_client64)
		std::memcpy(p, &argument, sizeof(argument));
	else
	{
		uint32_t argument32 = (uint32_t) argument;
		std::memcpy(p, &argument32, sizeof(argument32));
	}
	p += argumentSize;

	head->header.msgh_size = (mach_msg_size_t) (p - buffer);

	// Never block iokitd on a client that does not drain its port.
	kern_return_t kr = mach_msg(&head->header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, head->header.msgh_size,
		0, MACH_PORT_NULL, 0, MACH_PORT_NULL);

	if (kr != KERN_SUCCESS)
	{
		// A pseudo-receive on failed delivery returns newly copied rights.
		// mach_msg_destroy handles the remote right/descriptors, but not local.
		mach_msg_type_name_t local = MACH_MSGH_BITS_LOCAL(head->header.msgh_bits);
		if (local == MACH_MSG_TYPE_MOVE_SEND || local == MACH_MSG_TYPE_MOVE_SEND_ONCE)
			mach_port_deallocate(mach_task_self(), head->header.msgh_local_port);
		mach_msg_destroy(&head->header);
	}
	return kr;
}

bool IOInterestNotification::wakePortIsAlive() const
{
	mach_port_type_t type = 0;
	return mach_port_type(mach_task_self(), m_wakePort, &type) == KERN_SUCCESS
		&& (type & MACH_PORT_TYPE_SEND);
}

void IOInterestNotification::scheduleRetry()
{
	if (m_retryScheduled)
		return;
	m_retryScheduled = true;
	mach_port_t notificationPort = port();
	// Do not retain a send right while waiting: deregistration must still
	// destroy this object. Look it up again on the same serial queue.
	IOInterestNotification* expected = this;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
		auto current = dynamic_cast<IOInterestNotification*>(IOObject::lookup(notificationPort));
		if (current != expected)
			return;
		current->m_retryScheduled = false;
		current->flushMessages();
	});
}

kern_return_t IOInterestNotification::flushMessages()
{
	while (!m_messages.empty())
	{
		kern_return_t kr = MACH_SEND_INVALID_DEST;
		if (wakePortIsAlive())
		{
			const auto& message = m_messages.front();
			kr = sendMessage(message.type, message.argument);
			// Mach can report success if the destination died during the send.
			if (!wakePortIsAlive())
				kr = MACH_SEND_INVALID_DEST;
		}
		if (kr == MACH_SEND_TIMED_OUT || kr == MACH_SEND_INTERRUPTED || kr == MACH_SEND_NO_BUFFER)
		{
			scheduleRetry();
			return KERN_SUCCESS; // accepted; preserve message order until the port drains
		}
		if (kr != KERN_SUCCESS)
		{
			m_failed = true;
			m_messages.clear();
			os_log_error(OS_LOG_DEFAULT, "iokitd: notification destination unavailable: 0x%x", kr);
			mach_port_t notificationPort = port();
			IOInterestNotification* expected = this;
			// Defer the removal: the service may be walking its interest list.
			dispatch_async(dispatch_get_main_queue(), ^{
				auto current = dynamic_cast<IOInterestNotification*>(IOObject::lookup(notificationPort));
				if (current == expected)
					current->m_service->removeInterest(current);
			});
			return kr;
		}
		m_messages.pop_front();
	}
	return KERN_SUCCESS;
}

kern_return_t IOInterestNotification::deliver(uint32_t messageType, uint64_t argument)
{
	if (m_failed)
		return MACH_SEND_INVALID_DEST;
	m_messages.push_back({messageType, argument});
	return flushMessages();
}

static kern_return_t addInterestNotification(mach_port_t service, const char* interestType,
	mach_port_t wakePort, const void* reference, size_t referenceSize, bool client64,
	mach_port_t* notification)
{
	*notification = MACH_PORT_NULL;

	// On failure dispatch_mig_server destroys the request, including wakePort.
	// Only a successful registration takes ownership of that send right.
	IOService* s = dynamic_cast<IOService*>(IOObject::lookup(service));
	if (!s)
		return kIOReturnBadArgument;
	if (!MACH_PORT_VALID(wakePort))
		return kIOReturnBadArgument;
	if (!s->acceptsInterest(interestType))
		return kIOReturnUnsupported;

	IOInterestNotification* n;
	try
	{
		n = new IOInterestNotification(s, interestType, wakePort, reference, referenceSize, client64);
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: cannot create interest notification: %s", e.what());
		return kIOReturnNoResources;
	}

	*notification = n->port();
	// The reply carries a copy of the send right; drop ours afterwards.
	n->releaseLater();
	return kIOReturnSuccess;
}

kern_return_t is_io_service_add_interest_notification_64
(
	mach_port_t service,
	io_name_t type_of_interest,
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

	OSAsyncReference64 zreference = {};
	std::memcpy(zreference, reference, referenceCnt * sizeof(zreference[0]));

	return addInterestNotification(service, type_of_interest, wake_port, zreference,
		sizeof(zreference), true, notification);
}

kern_return_t is_io_service_add_interest_notification
(
	mach_port_t service,
	io_name_t type_of_interest,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
	if (referenceCnt > kOSAsyncRefCount)
	{
		*notification = MACH_PORT_NULL;
		return kIOReturnBadArgument;
	}

	io_async_ref_t zreference = {};
	std::memcpy(zreference, reference, referenceCnt * sizeof(zreference[0]));

	return addInterestNotification(service, type_of_interest, wake_port, zreference,
		sizeof(zreference), false, notification);
}
