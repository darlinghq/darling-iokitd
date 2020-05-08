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

#ifndef IOKITD_IOUSERCLIENT_H
#define IOKITD_IOUSERCLIENT_H
#include "IOService.h"
#include "IOUserClientApple.h"

// https://unix.superglobalmegacorp.com/xnu/newsrc/iokit/IOKit/IOUserClient.h.html
// http://www.alauda.ro/2013/12/iouserclient-and-ioexternalmethod/

class IOUserClient : public IOService
{
public:
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments);
	IOExternalMethod *getTargetAndMethodForIndex(IOService **targetP, UInt32 index);
	virtual IOExternalMethod *getExternalMethodForIndex(UInt32 index) = 0;
};

#endif
