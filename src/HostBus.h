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

/*
 * iokitd's connections to the host's D-Bus: the system bus for
 * systemd-logind and the user's session bus for the desktop's ScreenSaver
 * service. libdbus-1 is the host's, used through Darling's wrapper library.
 *
 * This file has no Darling dependencies, so it can also be built natively
 * on Linux against libdbus-1 for testing.
 */

#ifndef IOKITD_HOST_BUS_H
#define IOKITD_HOST_BUS_H

#include <dbus/dbus.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum iokitd_host_bus
{
	IOKITD_SYSTEM_BUS,
	IOKITD_SESSION_BUS,
};

/*
 * The bus address: DBUS_SYSTEM_BUS_ADDRESS or DBUS_SESSION_BUS_ADDRESS if it
 * is a single plain unix socket address, otherwise the standard system bus
 * socket, or $XDG_RUNTIME_DIR/bus for the session bus. Returns 0, or -1 if
 * there is no usable address.
 */
int iokitd_host_bus_address(enum iokitd_host_bus bus, char* address, size_t size);

/* A private connection to the bus, or NULL. Close with iokitd_host_bus_close(). */
DBusConnection* iokitd_host_bus_open(enum iokitd_host_bus bus);
void iokitd_host_bus_close(DBusConnection* connection);

/*
 * org.freedesktop.login1.Manager.Inhibit(what, who = "Darling", why, mode).
 * Returns the inhibitor descriptor, which holds the lock until it is closed,
 * or -1.
 */
int iokitd_logind_inhibit(DBusConnection* system_bus, const char* what, const char* why,
	const char* mode, int timeout_ms);

/*
 * org.freedesktop.ScreenSaver.Inhibit(application = "Darling", reason). The
 * inhibition lasts until UnInhibit or until the connection is closed.
 * Returns 0 and the cookie, or -1.
 */
int iokitd_screensaver_inhibit(DBusConnection* session_bus, const char* reason,
	uint32_t* cookie, int timeout_ms);
void iokitd_screensaver_uninhibit(DBusConnection* session_bus, uint32_t cookie, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
