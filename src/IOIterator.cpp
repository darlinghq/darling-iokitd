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

#include "IOIterator.h"
#include <IOKit/IOReturn.h>
extern "C" {
#include "iokitmigServer.h"
}

IOIterator::IOIterator(const std::vector<IOObject*>& objects)
: m_objects(objects)
{

}

void IOIterator::reset()
{
	m_pos = 0;
}

const char* IOIterator::className() const
{
	return "IOUserIterator";
}

IOObject* IOIterator::next()
{
	if (m_pos >= m_objects.size())
		return nullptr;
	return m_objects[m_pos++];
}

kern_return_t is_io_iterator_next
(
	mach_port_t iterator,
	mach_port_t *object
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	*object = MACH_PORT_NULL;

	if (iter != nullptr)
	{
		IOObject* next = iter->next();
		if (next != nullptr)
		{
			*object = next->port();
			return kIOReturnSuccess;
		}
		else
			return kIOReturnNoDevice;
	}
	else
		return kIOReturnBadArgument;
}

kern_return_t is_io_iterator_reset
(
	mach_port_t iterator
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	if (!iter)
		return kIOReturnBadArgument;
	else
	{
		iter->reset();
		return kIOReturnSuccess;
	}
}


kern_return_t is_io_iterator_is_valid
(
	mach_port_t iterator,
	boolean_t *is_valid
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	*is_valid = iter != nullptr;
    return kIOReturnSuccess;
}
