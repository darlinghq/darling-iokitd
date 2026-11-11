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
 * Game controllers of the host, seen through the Linux evdev interface
 * (/dev/input/event*), and their description as a HID device.
 *
 * iokitd publishes every host game controller as an IOHIDDevice. This file
 * is the part that talks to the host: it recognises game controllers among
 * the evdev nodes, describes each as a HID report descriptor, and folds
 * evdev events into the input report that descriptor describes.
 *
 * The event numbers below are the Linux input ABI (linux/input-event-codes.h)
 * and are spelled out here because the file is built for Darwin as well as,
 * for testing, natively on Linux. It has no Darling dependencies. Only
 * 64-bit hosts are supported (the layout of struct input_event is fixed to
 * the 64-bit one); elsewhere no controller is ever found.
 *
 * Mapping to HID (a property of the descriptor this file generates, which
 * consumers such as GameController rely on):
 *  - buttons BTN_SOUTH .. BTN_THUMBR (0x130 .. 0x13e) are Button page
 *    usages 1 .. 15; BTN_DPAD_UP, _DOWN, _LEFT, _RIGHT (0x220 .. 0x223) are
 *    usages 16 .. 19; BTN_TRIGGER .. BTN_BASE6 (0x120 .. 0x12f) are
 *    usages 20 .. 35;
 *  - ABS_X .. ABS_RZ (0 .. 5) are Generic Desktop X, Y, Z, Rx, Ry, Rz
 *    (usages 0x30 .. 0x35); ABS_HAT0X and ABS_HAT0Y together are one Hat
 *    switch (0x39), null state when centred;
 *  - ABS_THROTTLE, ABS_RUDDER, ABS_GAS and ABS_BRAKE are Simulation Controls
 *    Throttle (0xBB), Rudder (0xBA), Accelerator (0xC4) and Brake (0xC5).
 * The input report has no report ID: the buttons present, packed one bit
 * each and padded to a byte, then the hat (4 bits, padded to a byte) if there
 * is one, then the axes in the order of their usages, each 16 bits (signed
 * if its minimum is negative) or 32 bits if its range needs it.
 */

#ifndef IOKITD_HOST_GAMEPAD_H
#define IOKITD_HOST_GAMEPAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOKITD_GAMEPAD_MAX_BUTTONS 40
#define IOKITD_GAMEPAD_MAX_AXES 16

enum iokitd_gamepad_axis_kind
{
	IOKITD_GAMEPAD_AXIS,
	IOKITD_GAMEPAD_HAT,
};

struct iokitd_gamepad_button
{
	uint16_t code;       /* evdev key code */
	uint16_t usage;      /* Button page usage */
};

struct iokitd_gamepad_axis
{
	uint16_t code;       /* evdev axis code, ABS_HAT0X for the hat */
	uint16_t usage_page;
	uint16_t usage;
	int32_t minimum, maximum;
	uint8_t bits;        /* 16 or 32 */
	uint16_t byte_offset;/* in the input report */
};

struct iokitd_gamepad
{
	char path[64];
	char name[128];
	char serial[64];
	uint16_t bustype, vendor, product, version;

	int has_gamepad_buttons; /* the BTN_GAMEPAD block, as opposed to a joystick */

	int button_count;
	struct iokitd_gamepad_button buttons[IOKITD_GAMEPAD_MAX_BUTTONS];

	int axis_count;          /* without the hat */
	struct iokitd_gamepad_axis axes[IOKITD_GAMEPAD_MAX_AXES];

	int has_hat;
	int32_t hat_x_min, hat_x_max, hat_y_min, hat_y_max;
	uint16_t hat_byte_offset;

	uint16_t report_size;    /* bytes */
};

/*
 * Reads the capabilities of an evdev node and decides whether it is a game
 * controller: absolute X and Y axes plus the gamepad or joystick buttons,
 * and none of the markers of a touch surface, tablet or motion sensor.
 * Returns 0 and fills *pad, or -1 if the node cannot be opened (no
 * permission is not an error worth reporting: there is simply no device),
 * is not a controller, or is not usable.
 */
int iokitd_gamepad_probe(const char* path, struct iokitd_gamepad* pad);

/*
 * The paths of the evdev nodes under directory (normally /dev/input) that are
 * named event<N>, sorted by N, written into paths (each of size
 * sizeof(paths[0])). Returns how many there are, at most max.
 */
int iokitd_gamepad_list(const char* directory, char paths[][64], int max);

/* Opens a node for reading events: non-blocking, close-on-exec. Returns a descriptor or -1. */
int iokitd_gamepad_open(const char* path);

/*
 * Reads all pending events from fd and applies them to report (which is
 * pad->report_size bytes and holds the current state). Returns 1 if the
 * report changed, 0 if nothing relevant happened, -1 when the device is gone
 * (the descriptor must be closed). A report is only updated when the kernel
 * marks the end of an event frame (SYN_REPORT), so it is always consistent.
 */
int iokitd_gamepad_read(int fd, const struct iokitd_gamepad* pad, uint8_t* report);

/* The initial report: reads the current state of the node, which may have
 * buttons held when it is opened. */
void iokitd_gamepad_initial_report(int fd, const struct iokitd_gamepad* pad, uint8_t* report);

/*
 * Applies one event to the report. Exposed for the descriptor/report tests;
 * iokitd_gamepad_read() uses it. Returns 1 if the report changed.
 */
int iokitd_gamepad_apply_event(const struct iokitd_gamepad* pad, uint8_t* report,
	uint16_t type, uint16_t code, int32_t value);

/* Writes the HID report descriptor. Returns its length, or -1 if size is too small. */
int iokitd_gamepad_report_descriptor(const struct iokitd_gamepad* pad, uint8_t* out, size_t size);

/* Generic Desktop usage of the device: 5 (Game Pad) or 4 (Joystick). */
static inline uint16_t iokitd_gamepad_primary_usage(const struct iokitd_gamepad* pad)
{
	return pad->has_gamepad_buttons ? 5 : 4;
}

#ifdef __cplusplus
}
#endif

#endif
