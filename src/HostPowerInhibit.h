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
 * Host-side backing for power assertions:
 *
 * - idle: a systemd-logind "idle" inhibitor in "block" mode, so the host
 *   does not suspend or blank on idle;
 * - screen saver: org.freedesktop.ScreenSaver.Inhibit on the session bus,
 *   which desktops use to keep the display awake.
 *
 * Both are counted: the host lock is taken by the first acquire and dropped
 * by the matching last release. Not thread safe; iokitd calls these from
 * its main queue. The calls block for at most a few seconds, and only when
 * the bus is reachable but its peer does not answer.
 *
 * This file has no Darling dependencies, so it can also be built natively
 * on Linux against libdbus-1 for testing.
 */

#ifndef IOKITD_HOST_POWER_INHIBIT_H
#define IOKITD_HOST_POWER_INHIBIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the host lock is held after the call, 0 if it could not be taken. */
int iokitd_idle_inhibit_acquire(void);
void iokitd_idle_inhibit_release(void);

int iokitd_screensaver_inhibit_acquire(void);
void iokitd_screensaver_inhibit_release(void);

/* Drops both host locks and the bus connections, whatever the counts. */
void iokitd_host_inhibit_reset(void);

#ifdef __cplusplus
}
#endif

#endif
