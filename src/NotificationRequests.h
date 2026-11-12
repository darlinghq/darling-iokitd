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

#ifndef IOKITD_NOTIFICATIONREQUESTS_H
#define IOKITD_NOTIFICATIONREQUESTS_H
#include <mach/mach.h>
#include <stddef.h>

// Shared by the requests of 64-bit and 32-bit clients. reference holds
// referenceSize bytes of the client's async reference, whose words are 64 bits
// wide when client64 is set and 32 bits otherwise.
kern_return_t iokitAddServiceNotification(const char* notificationType, const char* matching,
	mach_msg_type_number_t matchingCnt, mach_port_t wakePort, const void* reference,
	size_t referenceSize, bool client64, mach_port_t* notification);

kern_return_t iokitAddInterestNotification(mach_port_t service, const char* interestType,
	mach_port_t wakePort, const void* reference, size_t referenceSize, bool client64,
	mach_port_t* notification);

#endif
