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

#include "LogindSleep.h"
#include "HostBus.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOGIND_NAME "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER "org.freedesktop.login1.Manager"

#define USEC_PER_SEC 1000000ULL
/* logind's default InhibitDelayMaxSec */
#define DEFAULT_LOGIND_DELAY_USEC (5 * USEC_PER_SEC)
/* What macOS gives applications to answer kIOMessageSystemWillSleep */
#define MAX_ACK_TIMEOUT_USEC (30 * USEC_PER_SEC)
#define MIN_ACK_TIMEOUT_USEC (USEC_PER_SEC / 10)
/* Upper bound on how long the bus thread waits before checking deadlines and stop */
#define POLL_INTERVAL_MS 200
#define CALL_TIMEOUT_MS 5000

#define PREPARE_FOR_SLEEP_MATCH "type='signal',sender='" LOGIND_NAME "',path='" LOGIND_PATH \
	"',interface='" LOGIND_MANAGER "',member='PrepareForSleep'"
#define OWNER_CHANGED_MATCH "type='signal',sender='" DBUS_SERVICE_DBUS "',path='" DBUS_PATH_DBUS \
	"',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='" LOGIND_NAME "'"

struct iokitd_logind
{
	struct iokitd_logind_callbacks callbacks;
	void* context;

	DBusConnection* bus;
	pthread_t thread;
	uint64_t ack_timeout_usec;

	/* Shared with iokitd_logind_sleep_ready() */
	pthread_mutex_t lock;
	int inhibitor_fd;
	int sleeping;
	uint64_t cycle;
	uint64_t deadline_usec;
	int stop;

	/* Bus thread only */
	int want_inhibitor;
};

static uint64_t now_usec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * USEC_PER_SEC + (uint64_t) ts.tv_nsec / 1000;
}

static void log_message(struct iokitd_logind* l, const char* format, ...)
	__attribute__((format(printf, 2, 3)));

static void log_message(struct iokitd_logind* l, const char* format, ...)
{
	char buffer[256];
	va_list ap;

	if (l->callbacks.log == NULL)
		return;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
	l->callbacks.log(l->context, buffer);
}

/* Must hold l->lock. */
static void close_inhibitor_locked(struct iokitd_logind* l)
{
	if (l->inhibitor_fd >= 0)
	{
		close(l->inhibitor_fd);
		l->inhibitor_fd = -1;
	}
}

static void take_inhibitor(struct iokitd_logind* l)
{
	int fd = iokitd_logind_inhibit(l->bus, "sleep", "Notify macOS applications before the system sleeps",
		"delay", CALL_TIMEOUT_MS);

	if (fd < 0)
	{
		log_message(l, "logind: cannot take a sleep delay lock");
		return;
	}
	pthread_mutex_lock(&l->lock);
	close_inhibitor_locked(l);
	l->inhibitor_fd = fd;
	pthread_mutex_unlock(&l->lock);
}

static void prepare_for_sleep(struct iokitd_logind* l, DBusMessage* message)
{
	DBusMessageIter iter;
	dbus_bool_t start;
	uint64_t cycle;

	if (!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_BOOLEAN)
		return;
	dbus_message_iter_get_basic(&iter, &start);

	pthread_mutex_lock(&l->lock);
	if (start)
	{
		l->cycle++;
		l->sleeping = 1;
		l->deadline_usec = now_usec() + l->ack_timeout_usec;
	}
	else
	{
		l->sleeping = 0;
		/* If logind suspended without waiting for us, the old lock is still valid. */
		l->want_inhibitor = l->inhibitor_fd < 0;
	}
	cycle = l->cycle;
	pthread_mutex_unlock(&l->lock);

	if (start)
	{
		if (l->callbacks.will_sleep)
			l->callbacks.will_sleep(l->context, cycle);
	}
	else if (l->callbacks.did_wake)
		l->callbacks.did_wake(l->context, cycle);
}

/* logind may appear after iokitd starts, or restart while we are awake. */
static void logind_owner_changed(struct iokitd_logind* l, DBusMessage* message)
{
	DBusMessageIter iter;
	const char* fields[3] = { NULL, NULL, NULL };

	if (!dbus_message_iter_init(message, &iter))
		return;
	for (int i = 0; i < 3; i++)
	{
		if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
			return;
		dbus_message_iter_get_basic(&iter, &fields[i]);
		dbus_message_iter_next(&iter);
	}
	if (strcmp(fields[0], LOGIND_NAME) != 0)
		return;

	pthread_mutex_lock(&l->lock);
	close_inhibitor_locked(l);
	l->sleeping = 0;
	l->want_inhibitor = fields[2][0] != '\0';
	pthread_mutex_unlock(&l->lock);
}

static void handle_messages(struct iokitd_logind* l)
{
	DBusMessage* message;

	while ((message = dbus_connection_pop_message(l->bus)) != NULL)
	{
		if (dbus_message_is_signal(message, LOGIND_MANAGER, "PrepareForSleep"))
			prepare_for_sleep(l, message);
		else if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
			logind_owner_changed(l, message);
		dbus_message_unref(message);
	}
}

static void close_bus(struct iokitd_logind* l)
{
	iokitd_host_bus_close(l->bus);
	l->bus = NULL;
}

static int connect_bus(struct iokitd_logind* l)
{
	DBusError error;

	l->bus = iokitd_host_bus_open(IOKITD_SYSTEM_BUS);
	if (l->bus == NULL)
		return -1;

	/* Subscribe before taking the lock so that no announcement is missed. */
	dbus_error_init(&error);
	dbus_bus_add_match(l->bus, PREPARE_FOR_SLEEP_MATCH, &error);
	if (!dbus_error_is_set(&error))
		dbus_bus_add_match(l->bus, OWNER_CHANGED_MATCH, &error);
	if (dbus_error_is_set(&error))
	{
		dbus_error_free(&error);
		close_bus(l);
		return -1;
	}
	return 0;
}

static void connection_lost(struct iokitd_logind* l)
{
	pthread_mutex_lock(&l->lock);
	close_inhibitor_locked(l);
	l->sleeping = 0;
	l->want_inhibitor = 0;
	pthread_mutex_unlock(&l->lock);
	close_bus(l);
}

static int should_stop(struct iokitd_logind* l)
{
	int stop;
	pthread_mutex_lock(&l->lock);
	stop = l->stop;
	pthread_mutex_unlock(&l->lock);
	return stop;
}

static void* bus_thread(void* arg)
{
	struct iokitd_logind* l = arg;
	uint64_t next_reconnect = 0, backoff = USEC_PER_SEC;

	while (!should_stop(l))
	{
		int wait_ms = POLL_INTERVAL_MS;
		uint64_t now;

		if (l->bus == NULL)
		{
			if (now_usec() < next_reconnect)
			{
				usleep(POLL_INTERVAL_MS * 1000);
				continue;
			}
			if (connect_bus(l) != 0)
			{
				next_reconnect = now_usec() + backoff;
				backoff = backoff < 15 * USEC_PER_SEC ? backoff * 2 : 30 * USEC_PER_SEC;
				continue;
			}
			backoff = USEC_PER_SEC;
			take_inhibitor(l);
			log_message(l, "logind: system bus reconnected");
		}

		handle_messages(l);

		if (l->want_inhibitor)
		{
			l->want_inhibitor = 0;
			take_inhibitor(l);
			/* The call may have queued signals. */
			continue;
		}

		now = now_usec();
		pthread_mutex_lock(&l->lock);
		if (l->sleeping && l->inhibitor_fd >= 0)
		{
			if (now >= l->deadline_usec)
			{
				log_message(l, "logind: applications did not acknowledge sleep in time");
				close_inhibitor_locked(l);
			}
			else if ((l->deadline_usec - now) / 1000 < (uint64_t) wait_ms)
				wait_ms = (int) ((l->deadline_usec - now) / 1000) + 1;
		}
		pthread_mutex_unlock(&l->lock);

		if (!dbus_connection_read_write(l->bus, wait_ms) || !dbus_connection_get_is_connected(l->bus))
		{
			/* Messages that arrived before the disconnection still count. */
			handle_messages(l);
			log_message(l, "logind: system bus connection lost, reconnecting");
			connection_lost(l);
			next_reconnect = 0;
		}
	}

	/* Never hold up a suspend once nobody is listening. */
	pthread_mutex_lock(&l->lock);
	close_inhibitor_locked(l);
	pthread_mutex_unlock(&l->lock);
	return NULL;
}

static uint64_t derive_ack_timeout(struct iokitd_logind* l)
{
	const char* args[] = { LOGIND_MANAGER, "InhibitDelayMaxUSec" };
	DBusMessage* message;
	DBusMessage* reply = NULL;
	DBusMessageIter iter, variant;
	DBusError error;
	uint64_t max_delay = 0, timeout;

	message = dbus_message_new_method_call(LOGIND_NAME, LOGIND_PATH, DBUS_INTERFACE_PROPERTIES, "Get");
	if (message != NULL)
	{
		dbus_message_iter_init_append(message, &iter);
		if (dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &args[0])
			&& dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &args[1]))
		{
			dbus_error_init(&error);
			reply = dbus_connection_send_with_reply_and_block(l->bus, message, CALL_TIMEOUT_MS, &error);
			dbus_error_free(&error);
		}
		dbus_message_unref(message);
	}
	if (reply != NULL)
	{
		if (dbus_message_iter_init(reply, &iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
		{
			dbus_message_iter_recurse(&iter, &variant);
			if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_UINT64)
			{
				dbus_uint64_t value;
				dbus_message_iter_get_basic(&variant, &value);
				max_delay = value;
			}
		}
		dbus_message_unref(reply);
	}
	if (max_delay == 0)
		max_delay = DEFAULT_LOGIND_DELAY_USEC;

	/* Release a second before logind gives up on us. */
	timeout = max_delay > 2 * USEC_PER_SEC ? max_delay - USEC_PER_SEC : max_delay / 2;
	if (timeout > MAX_ACK_TIMEOUT_USEC)
		timeout = MAX_ACK_TIMEOUT_USEC;
	if (timeout < MIN_ACK_TIMEOUT_USEC)
		timeout = MIN_ACK_TIMEOUT_USEC;
	return timeout;
}

static void destroy(struct iokitd_logind* l)
{
	/* No bus thread exists here (a startup failure, or stop has joined it). */
	close_bus(l);
	close_inhibitor_locked(l);
	pthread_mutex_destroy(&l->lock);
	free(l);
}

struct iokitd_logind* iokitd_logind_start(const struct iokitd_logind_callbacks* callbacks,
	void* context, uint64_t ack_timeout_usec)
{
	struct iokitd_logind* l;

	if (callbacks == NULL)
		return NULL;
	l = calloc(1, sizeof(*l));
	if (l == NULL)
		return NULL;
	l->callbacks = *callbacks;
	l->context = context;
	l->inhibitor_fd = -1;
	pthread_mutex_init(&l->lock, NULL);

	if (connect_bus(l) != 0)
	{
		log_message(l, "logind: the system bus is not available");
		destroy(l);
		return NULL;
	}

	l->ack_timeout_usec = ack_timeout_usec ? ack_timeout_usec : derive_ack_timeout(l);
	take_inhibitor(l);

	if (pthread_create(&l->thread, NULL, bus_thread, l) != 0)
	{
		destroy(l);
		return NULL;
	}
	return l;
}

void iokitd_logind_sleep_ready(struct iokitd_logind* l, uint64_t cycle)
{
	if (l == NULL)
		return;
	pthread_mutex_lock(&l->lock);
	if (l->sleeping && l->cycle == cycle)
		close_inhibitor_locked(l);
	pthread_mutex_unlock(&l->lock);
}

uint64_t iokitd_logind_ack_timeout_usec(const struct iokitd_logind* l)
{
	return l ? l->ack_timeout_usec : 0;
}

int iokitd_logind_holds_inhibitor(struct iokitd_logind* l)
{
	int held;

	if (l == NULL)
		return 0;
	pthread_mutex_lock(&l->lock);
	held = l->inhibitor_fd >= 0;
	pthread_mutex_unlock(&l->lock);
	return held;
}

void iokitd_logind_stop(struct iokitd_logind* l)
{
	if (l == NULL)
		return;
	pthread_mutex_lock(&l->lock);
	l->stop = 1;
	pthread_mutex_unlock(&l->lock);
	pthread_join(l->thread, NULL);
	destroy(l);
}
