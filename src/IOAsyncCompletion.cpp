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

#include "IOAsyncCompletion.h"
#include <IOKit/OSMessageNotification.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

// The message is the mach header, the notification header (content size, type
// and the async reference words), the result and the arguments. The arguments
// follow the result without padding.
static const size_t kResultOffset = sizeof(mach_msg_header_t) + offsetof(OSNotificationHeader64, reference)
	+ sizeof(OSAsyncReference64);

IOReturn iokitSendAsyncCompletion(mach_port_t wakePort, const uint64_t* reference,
	uint32_t referenceCount, IOReturn result, const uint64_t* args, uint32_t numArgs)
{
	if (!MACH_PORT_VALID(wakePort))
		return kIOReturnNotReady;
	if (numArgs > kMaxAsyncArgs)
		return kIOReturnMessageTooLarge;

	bool client64 = referenceCount >= 1 && (reference[0] & kIOAsyncReference64Flag);
	size_t wordSize = client64 ? sizeof(uint64_t) : sizeof(uint32_t);
	size_t refSize = client64 ? sizeof(OSAsyncReference64) : sizeof(OSAsyncReference);
	// Both layouts put the result after the references; only their width differs
	size_t resultOffset = sizeof(mach_msg_header_t) + offsetof(OSNotificationHeader64, reference) + refSize;
	size_t size = resultOffset + sizeof(IOReturn) + numArgs * wordSize;

	alignas(8) uint8_t buffer[kResultOffset + sizeof(IOReturn) + kMaxAsyncArgs * sizeof(uint64_t)];
	std::memset(buffer, 0, sizeof(buffer));

	mach_msg_header_t* head = reinterpret_cast<mach_msg_header_t*>(buffer);
	head->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	head->msgh_size = mach_msg_size_t(size);
	head->msgh_remote_port = wakePort;
	head->msgh_local_port = MACH_PORT_NULL;
	head->msgh_id = kOSNotificationMessageID;

	uint8_t* p = buffer + sizeof(mach_msg_header_t);
	// OSNotificationHeader(64): content size and type, then the reference
	mach_msg_size_t contentSize = mach_msg_size_t(sizeof(IOReturn) + numArgs * wordSize);
	natural_t type = kIOAsyncCompletionNotificationType;
	std::memcpy(p, &contentSize, sizeof(contentSize));
	std::memcpy(p + sizeof(contentSize), &type, sizeof(type));
	p += offsetof(OSNotificationHeader64, reference);

	// Word 0 is the client's marker and is not passed back
	for (uint32_t i = 1; i < std::min<uint32_t>(referenceCount, kOSAsyncRef64Count); i++)
	{
		if (client64)
			std::memcpy(p + i * sizeof(uint64_t), &reference[i], sizeof(uint64_t));
		else
		{
			uint32_t word = uint32_t(reference[i]);
			std::memcpy(p + i * sizeof(uint32_t), &word, sizeof(word));
		}
	}
	p += refSize;

	std::memcpy(p, &result, sizeof(result));
	p += sizeof(result);
	for (uint32_t i = 0; i < numArgs; i++)
	{
		if (client64)
			std::memcpy(p + i * sizeof(uint64_t), &args[i], sizeof(uint64_t));
		else
		{
			uint32_t word = uint32_t(args[i]);
			std::memcpy(p + i * sizeof(uint32_t), &word, sizeof(word));
		}
	}

	kern_return_t kr = mach_msg(head, MACH_SEND_MSG | MACH_SEND_TIMEOUT, head->msgh_size, 0,
		MACH_PORT_NULL, 0, MACH_PORT_NULL);

	if (kr == KERN_SUCCESS)
		return kIOReturnSuccess;
	return kr == MACH_SEND_TIMED_OUT ? kIOReturnNoResources : kIOReturnNoDevice;
}
