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

#include "IOServiceNotification.h"
#include "IOService.h"
#include "ServiceRegistry.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

// IODispatchCalloutFromMessage() reads this: the notification header follows
// the Mach header directly in a message without descriptors.
template <class Header>
struct MatchingMessage
{
	mach_msg_header_t header;
	Header notification;
};

static_assert(offsetof(MatchingMessage<OSNotificationHeader64>, notification) == sizeof(mach_msg_header_t), "unexpected message layout");
static_assert(offsetof(MatchingMessage<OSNotificationHeader>, notification) == sizeof(mach_msg_header_t), "unexpected message layout");

// Fills in the notification header (the reference is in the width the client uses)
// and sends the message to the wake port.
template <class Header>
static kern_return_t sendMatchingMessage(mach_port_t wakePort, mach_port_t notificationPort,
	natural_t type, const void* reference)
{
	MatchingMessage<Header> message;
	std::memset(&message, 0, sizeof(message));

	// The client checks that it still holds this notification: it receives it
	// as msgh_remote_port, and a reference carried by the message is one of two.
	message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
	message.header.msgh_size = sizeof(message);
	message.header.msgh_remote_port = wakePort;
	message.header.msgh_local_port = notificationPort;
	message.header.msgh_id = kOSNotificationMessageID;

	message.notification.size = 0;
	message.notification.type = type;
	std::memcpy(message.notification.reference, reference, sizeof(message.notification.reference));

	// Never block iokitd on a client that does not drain its port: a full
	// port means the client has a wakeup pending already.
	kern_return_t kr = mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message),
		0, MACH_PORT_NULL, 0, MACH_PORT_NULL);

	if (kr != KERN_SUCCESS)
	{
		// A failed send leaves the rights it made in the header; give them back.
		mach_msg_type_name_t local = MACH_MSGH_BITS_LOCAL(message.header.msgh_bits);
		if (local == MACH_MSG_TYPE_MOVE_SEND || local == MACH_MSG_TYPE_MOVE_SEND_ONCE)
			mach_port_deallocate(mach_task_self(), message.header.msgh_local_port);
		mach_msg_destroy(&message.header);
	}
	return kr;
}

IOServiceNotification::IOServiceNotification(Kind kind, NSDictionary* matching, mach_port_t wakePort,
	const void* reference, size_t referenceSize, bool client64)
: IOIterator(std::vector<IOObject*>()), m_kind(kind), m_matching(matching), m_wakePort(wakePort),
	m_client64(client64)
{
	std::memset(m_reference, 0, sizeof(m_reference));
	std::memcpy(m_reference, reference, std::min(referenceSize, sizeof(m_reference)));
	ServiceRegistry::instance()->addNotification(this);
}

IOServiceNotification::~IOServiceNotification()
{
	ServiceRegistry::instance()->removeNotification(this);
	if (MACH_PORT_VALID(m_wakePort))
		mach_port_deallocate(mach_task_self(), m_wakePort);
	[m_matching release];
}

const char* IOServiceNotification::className() const
{
	return "IOServiceUserNotification";
}

void IOServiceNotification::serviceEvent(IOService* service)
{
	if (!service->matches(m_matching))
		return;
	append(service);
	wake();
}

void IOServiceNotification::wake()
{
	if (!MACH_PORT_VALID(m_wakePort))
		return;

	natural_t type = 0;
	switch (m_kind)
	{
		case Publish: type = kIOServicePublishNotificationType; break;
		case Match: type = kIOServiceMatchedNotificationType; break;
		case Terminate: type = kIOServiceTerminatedNotificationType; break;
	}

	if (m_client64)
		sendMatchingMessage<OSNotificationHeader64>(m_wakePort, port(), type, m_reference);
	else
		sendMatchingMessage<OSNotificationHeader>(m_wakePort, port(), type, m_reference);
}
