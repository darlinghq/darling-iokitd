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

#ifndef IOKITD_IOSERVICE_H
#define IOKITD_IOSERVICE_H
#include <Foundation/NSDictionary.h>
#include "IORegistryEntry.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitLibPrivate.h>

class IOService : public IORegistryEntry
{
public:
	IOService();
	virtual NSDictionary* matchingDictionary() = 0;
	bool matches(NSDictionary* dict);
	bool conformsTo(const char* className) override;

	uint32_t busyState() const { return m_busyState; }
	uint64_t accumulatedBusyTime() const { return m_accumulatedBusyTime; }
	uint64_t state() const { return m_state; }
protected:
	uint32_t m_busyState = 0;
	uint64_t m_accumulatedBusyTime = 0;
	uint64_t m_state = kIOServiceRegisteredState | kIOServiceMatchedState;
};

#endif
