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

#include "HostPowerInhibit.h"
#include "HostBus.h"
#include <stdint.h>
#include <unistd.h>

#define CALL_TIMEOUT_MS 2000
#define REASON "A macOS application is preventing sleep"

struct inhibitor
{
	enum iokitd_host_bus bus_kind;
	DBusConnection* bus;
	unsigned int count;
	int held;
	/* idle: the logind descriptor; screen saver: the cookie */
	int fd;
	uint32_t cookie;
};

static struct inhibitor g_idle = { IOKITD_SYSTEM_BUS, NULL, 0, 0, -1, 0 };
static struct inhibitor g_screensaver = { IOKITD_SESSION_BUS, NULL, 0, 0, -1, 0 };

static DBusConnection* bus_for(struct inhibitor* inh)
{
	if (inh->bus != NULL && !dbus_connection_get_is_connected(inh->bus))
	{
		iokitd_host_bus_close(inh->bus);
		inh->bus = NULL;
	}
	if (inh->bus == NULL)
		inh->bus = iokitd_host_bus_open(inh->bus_kind);
	return inh->bus;
}

static int take(struct inhibitor* inh)
{
	DBusConnection* bus = bus_for(inh);

	if (bus == NULL)
		return 0;
	if (inh == &g_idle)
	{
		inh->fd = iokitd_logind_inhibit(bus, "idle", REASON, "block", CALL_TIMEOUT_MS);
		return inh->fd >= 0;
	}
	return iokitd_screensaver_inhibit(bus, REASON, &inh->cookie, CALL_TIMEOUT_MS) == 0;
}

static void drop(struct inhibitor* inh)
{
	if (inh == &g_idle)
	{
		if (inh->fd >= 0)
			close(inh->fd);
		inh->fd = -1;
	}
	else if (inh->bus != NULL && dbus_connection_get_is_connected(inh->bus))
		iokitd_screensaver_uninhibit(inh->bus, inh->cookie, CALL_TIMEOUT_MS);
	inh->held = 0;
}

static int acquire(struct inhibitor* inh)
{
	if (inh->count++ == 0)
		inh->held = take(inh);
	return inh->held;
}

static void release(struct inhibitor* inh)
{
	if (inh->count == 0)
		return;
	if (--inh->count == 0 && inh->held)
		drop(inh);
}

int iokitd_idle_inhibit_acquire(void)
{
	return acquire(&g_idle);
}

void iokitd_idle_inhibit_release(void)
{
	release(&g_idle);
}

int iokitd_screensaver_inhibit_acquire(void)
{
	return acquire(&g_screensaver);
}

void iokitd_screensaver_inhibit_release(void)
{
	release(&g_screensaver);
}

void iokitd_host_inhibit_reset(void)
{
	struct inhibitor* all[] = { &g_idle, &g_screensaver };

	for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++)
	{
		if (all[i]->held)
			drop(all[i]);
		all[i]->count = 0;
		iokitd_host_bus_close(all[i]->bus);
		all[i]->bus = NULL;
	}
}
