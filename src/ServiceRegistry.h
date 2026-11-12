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

#ifndef IOKITD_REGISTRY_H
#define IOKITD_REGISTRY_H
#include "IOService.h"
#include <list>
#include <vector>
#import <Foundation/NSDictionary.h>
#include "IOIterator.h"

class IOServiceNotification;

class ServiceRegistry
{
private:
	ServiceRegistry() {}
public:
	static ServiceRegistry* instance();
	// Publishes a service: notifies the matching notifications that are armed
	void registerService(IOService* service);
	// A service went away (e.g. an unplugged device). Notifies the termination
	// notifications. The object itself is not freed: clients may still hold
	// its port, and every call on it fails once it left the registry.
	void unregisterService(IOService* service);

	void addNotification(IOServiceNotification* notification);
	void removeNotification(IOServiceNotification* notification);
	std::vector<IOObject*> matchingServices(NSDictionary* criteria) const;
	IOIterator* iteratorForMatchingServices(NSDictionary* criteria) const;
	IOService* firstMatchingService(NSDictionary* criteria) const;
private:
	std::list<IOService*> m_registeredServices;
	std::list<IOServiceNotification*> m_notifications;
};

#endif
