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

/*
 * Follows host suspend and resume through systemd-logind on the system bus.
 *
 * At start a "delay" sleep inhibitor is taken. When logind announces a
 * suspend (PrepareForSleep(true)), will_sleep() is called and the inhibitor
 * is kept until iokitd_logind_sleep_ready() is called for that cycle or the
 * acknowledgement timeout expires, whichever comes first; then it is closed
 * so that the suspend can proceed. After resume (PrepareForSleep(false)),
 * did_wake() is called and a new inhibitor is taken. If logind restarts or
 * the bus connection is lost, the monitor reconnects.
 *
 * One background thread owns the bus connection; the callbacks run on it.
 * iokitd_logind_sleep_ready() may be called from any thread.
 *
 * This file has no Darling dependencies, so it can also be built natively
 * on Linux against libdbus-1 for testing.
 */

#ifndef IOKITD_LOGIND_SLEEP_H
#define IOKITD_LOGIND_SLEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iokitd_logind_callbacks
{
	/* A suspend is imminent. Call iokitd_logind_sleep_ready(cycle) once clients agreed. */
	void (*will_sleep)(void* context, uint64_t cycle);
	/* The system has resumed (or the suspend failed). */
	void (*did_wake)(void* context, uint64_t cycle);
	/* Optional diagnostics */
	void (*log)(void* context, const char* message);
};

struct iokitd_logind;

/*
 * Connects to the system bus, subscribes to PrepareForSleep, takes the delay
 * inhibitor and starts the bus thread. A missing logind is not an error: no
 * events arrive then. ack_timeout_usec is how long applications get to
 * acknowledge a suspend; 0 derives it from logind's InhibitDelayMaxUSec.
 * Returns NULL if there is no system bus.
 */
struct iokitd_logind* iokitd_logind_start(const struct iokitd_logind_callbacks* callbacks,
	void* context, uint64_t ack_timeout_usec);

/* Releases the inhibitor if it is still held for this suspend cycle. */
void iokitd_logind_sleep_ready(struct iokitd_logind* logind, uint64_t cycle);

/* The acknowledgement timeout in use, in microseconds. */
uint64_t iokitd_logind_ack_timeout_usec(const struct iokitd_logind* logind);

/* 1 if a delay inhibitor is currently held. */
int iokitd_logind_holds_inhibitor(struct iokitd_logind* logind);

/* Stops the bus thread and closes the inhibitor and the bus. */
void iokitd_logind_stop(struct iokitd_logind* logind);

#ifdef __cplusplus
}
#endif

#endif
