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

#include "iokitDispatch.h"
#include "NotificationRequests.h"
#include <IOKit/IOReturn.h>
#include <IOKit/OSMessageNotification.h>
#include <cstring>

extern "C" {
#include "iokitmig32Server.h"

// The 64-bit subsystem. Its generated header is not included here because it
// declares the same request types as the 32-bit one.
boolean_t iokit_server(mach_msg_header_t* request, mach_msg_header_t* reply);
}

boolean_t iokitDispatch(mach_msg_header_t* request, mach_msg_header_t* reply)
{
	if (iokit32_server(request, reply))
		return TRUE;
	return iokit_server(request, reply);
}

kern_return_t is32_io_service_add_notification_bin
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref32_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
	if (referenceCnt > kOSAsyncRefCount)
	{
		*notification = MACH_PORT_NULL;
		return kIOReturnBadArgument;
	}

	OSAsyncReference zreference = {};
	std::memcpy(zreference, reference, referenceCnt * sizeof(zreference[0]));

	return iokitAddServiceNotification(notification_type, matching, matchingCnt, wake_port,
		zreference, sizeof(zreference), false, notification);
}

kern_return_t is32_io_service_add_interest_notification
(
	mach_port_t service,
	io_name_t type_of_interest,
	mach_port_t wake_port,
	io_async_ref32_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
	if (referenceCnt > kOSAsyncRefCount)
	{
		*notification = MACH_PORT_NULL;
		return kIOReturnBadArgument;
	}

	OSAsyncReference zreference = {};
	std::memcpy(zreference, reference, referenceCnt * sizeof(zreference[0]));

	return iokitAddInterestNotification(service, type_of_interest, wake_port, zreference,
		sizeof(zreference), false, notification);
}
