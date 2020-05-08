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


#ifndef IOKITD_IODISPLAYCONNECTX11_H
#define IOKITD_IODISPLAYCONNECTX11_H
#include "IODisplayConnect.h"
#include "ServiceRegistry.h"
#include <vector>
#include <memory>
#include <X11/Xlib.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>

class IODisplayConnectX11 : IODisplayConnect
{
private:
	IODisplayConnectX11(int index, NSDictionary* props);

public:
	~IODisplayConnectX11();
	static void discoverDevices(ServiceRegistry* targetRegistry);
	NSDictionary* getProperties() override;

	// TODO: Make the whole display server abstraction nicer?
	static Display* getDisplay() { return m_display; }
private:
	static Display* m_display;
	// Index of the corresponding XrandR output
	int m_index;

	NSDictionary* m_props;
	static IORegistryEntry* m_root;
};

#endif
