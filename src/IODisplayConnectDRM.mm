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

#include "IODisplayConnectDRM.h"
#include "HostDisplay.h"
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>

static const size_t kMaxConnectors = 64;

IODisplayConnectDRM::IODisplayConnectDRM(NSDictionary* props)
{
	m_props = [props retain];
}

IODisplayConnectDRM::~IODisplayConnectDRM()
{
	[m_props release];
}

NSDictionary* IODisplayConnectDRM::getProperties()
{
	return m_props;
}

size_t IODisplayConnectDRM::discoverDevices(ServiceRegistry* targetServiceRegistry, const char* sysfsDrmDirectory)
{
	host_drm_connector connectors[kMaxConnectors];
	size_t count = host_drm_connectors(sysfsDrmDirectory, connectors, kMaxConnectors);

	if (count == 0)
		return 0;

	IORegistryEntry* root = new IORegistryEntry;
	root->registerInPlane(kIOServicePlane, "DRMDisplay", IORegistryEntry::root());

	for (size_t i = 0; i < count; i++)
	{
		NSData* edid = connectors[i].edid ? [NSData dataWithBytes: connectors[i].edid length: connectors[i].edidLength] : nil;
		NSString* name = [NSString stringWithUTF8String: connectors[i].name];
		// the display's own name when it has one, like the display of a Mac
		NSDictionary* props = makeProperties(name, edid, int(i), true);

		IODisplayConnectDRM* ioDisplay = new IODisplayConnectDRM(props);
		targetServiceRegistry->registerService(ioDisplay);
		ioDisplay->registerInPlane(kIOServicePlane, connectors[i].name, root);

		host_drm_connector_free(&connectors[i]);
	}
	return count;
}
