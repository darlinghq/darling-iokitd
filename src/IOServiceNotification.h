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

#ifndef IOKITD_IOSERVICENOTIFICATION_H
#define IOKITD_IOSERVICENOTIFICATION_H
#include "IOIterator.h"
#include <IOKit/OSMessageNotification.h>
#import <Foundation/NSDictionary.h>

class IOService;

// The object IOServiceAddMatchingNotification() returns: an iterator over the
// services that matched, which also wakes the client's notification port
// whenever more services arrive. The client then drains the iterator
// (IODispatchCalloutFromMessage() calls its callback with it).
class IOServiceNotification : public IOIterator
{
public:
	enum Kind
	{
		Publish,    // kIOPublishNotification, kIOFirstPublishNotification
		Match,      // kIOMatchedNotification, kIOFirstMatchNotification
		Terminate,  // kIOTerminatedNotification, kIOWillTerminateNotification, ...
	};

	// Takes over the send right to wakePort and the reference to matching.
	// reference holds referenceSize bytes of the client's async reference, made of
	// 64-bit words if client64 is set and of 32-bit words otherwise.
	IOServiceNotification(Kind kind, NSDictionary* matching, mach_port_t wakePort,
		const void* reference, size_t referenceSize, bool client64);
	~IOServiceNotification();

	const char* className() const override;

	Kind kind() const { return m_kind; }
	NSDictionary* matching() const { return m_matching; }

	// A service was published or terminated: queued if it matches, and the client woken
	void serviceEvent(IOService* service);
private:
	void wake();

	Kind m_kind;
	NSDictionary* m_matching;
	mach_port_t m_wakePort;
	OSAsyncReference64 m_reference;
	bool m_client64;
};

#endif
