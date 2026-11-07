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

#include "HostBus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SYSTEM_BUS "unix:path=/run/dbus/system_bus_socket"

#define LOGIND_NAME "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER "org.freedesktop.login1.Manager"

#define SCREENSAVER_NAME "org.freedesktop.ScreenSaver"
#define SCREENSAVER_PATH "/org/freedesktop/ScreenSaver"
#define SCREENSAVER_INTERFACE "org.freedesktop.ScreenSaver"

/*
 * D-Bus separates alternative addresses with ';' and accepts "unixexec:",
 * which runs a program, and "autolaunch:", which may start a bus. Only one
 * plain socket address (a path or an abstract name) is taken from the
 * environment.
 */
static int address_allowed(const char* address)
{
	if (strncmp(address, "unix:path=", 10) != 0 && strncmp(address, "unix:abstract=", 14) != 0)
		return 0;
	for (const unsigned char* c = (const unsigned char*) address; *c != '\0'; c++)
	{
		if (*c <= ' ' || *c >= 0x7f || *c == ';')
			return 0;
	}
	return strstr(address, "unixexec") == NULL;
}

int iokitd_host_bus_address(enum iokitd_host_bus bus, char* address, size_t size)
{
	const char* env = getenv(bus == IOKITD_SYSTEM_BUS ? "DBUS_SYSTEM_BUS_ADDRESS" : "DBUS_SESSION_BUS_ADDRESS");

	if (env != NULL && env[0] != '\0')
	{
		if (!address_allowed(env) || strlen(env) >= size)
			return -1;
		strcpy(address, env);
		return 0;
	}

	if (bus == IOKITD_SYSTEM_BUS)
	{
		if (strlen(DEFAULT_SYSTEM_BUS) >= size)
			return -1;
		strcpy(address, DEFAULT_SYSTEM_BUS);
		return 0;
	}

	const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
	char path[1024];
	if (runtime_dir == NULL || runtime_dir[0] != '/')
		return -1;
	if (snprintf(path, sizeof(path), "%s/bus", runtime_dir) >= (int) sizeof(path))
		return -1;

	char* escaped = dbus_address_escape_value(path);
	int result = -1;
	if (escaped != NULL && snprintf(address, size, "unix:path=%s", escaped) < (int) size)
		result = 0;
	dbus_free(escaped);
	return result;
}

DBusConnection* iokitd_host_bus_open(enum iokitd_host_bus bus)
{
	char address[1024];
	DBusConnection* connection;
	DBusError error;

	if (iokitd_host_bus_address(bus, address, sizeof(address)) != 0)
		return NULL;

	dbus_error_init(&error);
	connection = dbus_connection_open_private(address, &error);
	if (connection != NULL && !dbus_bus_register(connection, &error))
	{
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		connection = NULL;
	}
	dbus_error_free(&error);
	if (connection != NULL)
		dbus_connection_set_exit_on_disconnect(connection, FALSE);
	return connection;
}

void iokitd_host_bus_close(DBusConnection* connection)
{
	if (connection == NULL)
		return;
	dbus_connection_close(connection);
	dbus_connection_unref(connection);
}

static int append_strings(DBusMessage* message, const char* const* strings, size_t count)
{
	DBusMessageIter args;

	dbus_message_iter_init_append(message, &args);
	for (size_t i = 0; i < count; i++)
	{
		if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &strings[i]))
			return 0;
	}
	return 1;
}

/* Sends the call and returns the reply, or NULL. Takes the call. */
static DBusMessage* call(DBusConnection* connection, DBusMessage* message, int timeout_ms)
{
	DBusMessage* reply;
	DBusError error;

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(connection, message, timeout_ms, &error);
	dbus_error_free(&error);
	dbus_message_unref(message);
	return reply;
}

int iokitd_logind_inhibit(DBusConnection* system_bus, const char* what, const char* why,
	const char* mode, int timeout_ms)
{
	const char* args[] = { what, "Darling", why, mode };
	DBusMessage* message;
	DBusMessage* reply;
	DBusMessageIter iter;
	int fd = -1;

	if (system_bus == NULL)
		return -1;
	message = dbus_message_new_method_call(LOGIND_NAME, LOGIND_PATH, LOGIND_MANAGER, "Inhibit");
	if (message == NULL)
		return -1;
	if (!append_strings(message, args, sizeof(args) / sizeof(args[0])))
	{
		dbus_message_unref(message);
		return -1;
	}

	reply = call(system_bus, message, timeout_ms);
	if (reply == NULL)
		return -1;
	/* The descriptor libdbus returns is a duplicate that belongs to us. */
	if (dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UNIX_FD)
		dbus_message_iter_get_basic(&iter, &fd);
	dbus_message_unref(reply);
	return fd;
}

int iokitd_screensaver_inhibit(DBusConnection* session_bus, const char* reason,
	uint32_t* cookie, int timeout_ms)
{
	const char* args[] = { "Darling", reason };
	DBusMessage* message;
	DBusMessage* reply;
	DBusMessageIter iter;
	int result = -1;

	if (session_bus == NULL)
		return -1;
	message = dbus_message_new_method_call(SCREENSAVER_NAME, SCREENSAVER_PATH, SCREENSAVER_INTERFACE, "Inhibit");
	if (message == NULL)
		return -1;
	if (!append_strings(message, args, sizeof(args) / sizeof(args[0])))
	{
		dbus_message_unref(message);
		return -1;
	}

	reply = call(session_bus, message, timeout_ms);
	if (reply == NULL)
		return -1;
	if (dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UINT32)
	{
		dbus_uint32_t value;
		dbus_message_iter_get_basic(&iter, &value);
		*cookie = value;
		result = 0;
	}
	dbus_message_unref(reply);
	return result;
}

void iokitd_screensaver_uninhibit(DBusConnection* session_bus, uint32_t cookie, int timeout_ms)
{
	DBusMessage* message;
	DBusMessage* reply;
	DBusMessageIter args;
	dbus_uint32_t value = cookie;

	if (session_bus == NULL)
		return;
	message = dbus_message_new_method_call(SCREENSAVER_NAME, SCREENSAVER_PATH, SCREENSAVER_INTERFACE, "UnInhibit");
	if (message == NULL)
		return;
	dbus_message_iter_init_append(message, &args);
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &value))
	{
		dbus_message_unref(message);
		return;
	}
	reply = call(session_bus, message, timeout_ms);
	if (reply != NULL)
		dbus_message_unref(reply);
}
