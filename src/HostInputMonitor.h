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

#ifndef IOKITD_HOSTINPUTMONITOR_H
#define IOKITD_HOSTINPUTMONITOR_H

#include "ServiceRegistry.h"

// Publishes the host's input devices, read from the evdev nodes the user can access, as
// IOHIDDevice services: game controllers (IOHIDGamepadDevice) and keyboards and mice
// (IOHIDInputDevice). One monitor looks at /dev/input once a second (poll rather than
// inotify: the Darwin guest has none), classifies each new node once, publishes a service for
// it and unpublishes the service when the node goes away or reading it fails. Without
// permission to read the nodes nothing is published, which is not an error.
class HostInputMonitor
{
public:
	static void start(ServiceRegistry* registry);
};

#endif
