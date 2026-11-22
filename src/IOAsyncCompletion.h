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

#ifndef IOKITD_IOASYNCCOMPLETION_H
#define IOKITD_IOASYNCCOMPLETION_H
#include <IOKit/IOReturn.h>
#include <mach/mach.h>
#include <stdint.h>

// Bit of word 0 of the async reference of IOConnectCallAsyncMethod() that the
// client sets when it is a 64-bit process. IOKitLib stamps it into the
// reference it sends, the way the XNU server does for a 64-bit task; the
// request carries nothing else that tells the width of the client.
enum : uint64_t { kIOAsyncReference64Flag = 1 };

// Sends the completion message of an asynchronous call (the IOKit completion
// notification that IODispatchCalloutFromMessage() of the client turns into a
// call of the callback in words 1 and 2 of the reference) to wakePort, with
// the result and numArgs further arguments. The words are 64 bits wide for a
// 64-bit client and 32 bits wide otherwise. The send does not block: a wake
// port with a full queue gets no message and kIOReturnNoResources. A
// completion is sent at most once per call; the send right of wakePort stays
// the caller's.
IOReturn iokitSendAsyncCompletion(mach_port_t wakePort, const uint64_t* reference,
	uint32_t referenceCount, IOReturn result, const uint64_t* args, uint32_t numArgs);

#endif
