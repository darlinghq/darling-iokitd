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

#ifndef IOKITD_IOINTERESTNOTIFICATION_H
#define IOKITD_IOINTERESTNOTIFICATION_H
#include "IOObject.h"
#include <IOKit/OSMessageNotification.h>
#include <string>
#include <deque>

class IOService;

// The object returned by IOServiceAddInterestNotification(). It lives until
// the client releases it, and sends service messages to the client's
// notification port in the layout IODispatchCalloutFromMessage() decodes.
class IOInterestNotification : public IOObject
{
public:
	// Takes over the send right to wakePort. reference holds referenceSize bytes
	// of the client's async reference (io_async_ref64_t or io_async_ref_t).
	IOInterestNotification(IOService* service, const char* interestType, mach_port_t wakePort,
		const void* reference, size_t referenceSize, bool client64);
	~IOInterestNotification();

	const char* className() const override;

	const std::string& interestType() const { return m_interestType; }
	IOService* service() const { return m_service; }

	// Queues messageType without blocking; retries in order when the port is full.
	kern_return_t deliver(uint32_t messageType, uint64_t argument);
private:
	kern_return_t sendMessage(uint32_t messageType, uint64_t argument);
	kern_return_t flushMessages();
	void scheduleRetry();
	bool wakePortIsAlive() const;
	struct Message { uint32_t type; uint64_t argument; };
	std::deque<Message> m_messages;
	bool m_retryScheduled = false;
	bool m_failed = false;
	IOService* m_service;
	std::string m_interestType;
	mach_port_t m_wakePort;
	OSAsyncReference64 m_reference;
	size_t m_referenceSize;
	bool m_client64;
};

#endif
