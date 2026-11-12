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

#ifndef IOKITD_IODISPLAYCONNECTDRM_H
#define IOKITD_IODISPLAYCONNECTDRM_H
#include "IODisplayConnect.h"
#include "ServiceRegistry.h"

// A display found through the DRM connectors of the host kernel, so that
// displays are known without an X server (Wayland, a console, a remote session).
class IODisplayConnectDRM : public IODisplayConnect
{
private:
	IODisplayConnectDRM(NSDictionary* props);

public:
	~IODisplayConnectDRM();
	NSDictionary* getProperties() override;

	// Publishes a service for each connected, enabled connector in the sysfs DRM class directory
	// (the host's /sys is visible under the system root) and returns how many there are.
	static size_t discoverDevices(ServiceRegistry* targetRegistry, const char* sysfsDrmDirectory = "/Volumes/SystemRoot/sys/class/drm");

private:
	NSDictionary* m_props;
};

#endif
