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
#include <stdexcept>
#include <stdarg.h>
#include <string>
#include <bsm/libbsm.h>
#include <IOKit/IOReturn.h>
#include "iokitDispatch.h"

extern "C" {
#include "iokitmigServer.h"
}

pid_t g_iokitCurrentCallerPID;

void throwCFStringException(CFStringRef format, ...)
{
	CFStringRef text;

	va_list ap;
	va_start(ap, format);
	
	text = CFStringCreateWithFormatAndArguments(nullptr, nullptr, format, ap);

	va_end(ap);

	std::string str = CFStringGetCStringPtr(text, kCFStringEncodingASCII);
	CFRelease(text);

	throw std::runtime_error(str);
}

boolean_t iokitSaveAuditTrail(mach_msg_header_t *message, mach_msg_header_t *reply)
{
	const mach_msg_context_trailer_t* trailer = reinterpret_cast<mach_msg_context_trailer_t*>(reinterpret_cast<char*>(message) + message->msgh_size);
	audit_token_to_au32(trailer->msgh_audit, nullptr, nullptr, nullptr, nullptr, nullptr, &g_iokitCurrentCallerPID, nullptr, nullptr);
	return iokitDispatch(message, reply);
}

// IOKit sends the binary forms of its requests (io_service_add_notification_bin and friends) only when the server
// reports 20140421 or later; the string forms are unsupported, as in XNU.
kern_return_t is_io_server_version
(
	mach_port_t master_port,
	uint64_t *version
)
{
	*version = IOKIT_SERVER_VERSION;
	return kIOReturnSuccess;
}
