/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Developers

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

#ifndef IOKITD_IOPLATFORMEXPERTDEVICE_H
#define IOKITD_IOPLATFORMEXPERTDEVICE_H

#include "IOService.h"
#include "ServiceRegistry.h"

// The platform expert device: the service IOServiceMatching("IOPlatformExpertDevice")
// finds, which carries the Mac's identity. The values come from the platform identity
// darlingserver holds (the org.darlinghq.platform preferences domain, or its defaults),
// through the same per-process snapshot that sysctl hw.model and gethostuuid() report.
class IOPlatformExpertDevice : public IOService
{
private:
	IOPlatformExpertDevice();
public:
	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	NSDictionary* getProperties() override;

	static void registerSelf(ServiceRegistry* targetServiceRegistry);
private:
	NSDictionary* m_properties;
};

#endif
