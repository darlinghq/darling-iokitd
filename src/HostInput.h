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
 * Keyboards and mice of the host, seen through the Linux evdev interface
 * (/dev/input/event*), and their description as HID devices.
 *
 * Like HostGamepad.c, this file is the part of iokitd that talks to the host
 * for the input devices published as IOHIDDevice services: it recognises
 * keyboards and mice among the evdev nodes, describes each as a HID report
 * descriptor and folds evdev events into the input report that descriptor
 * describes. It has no Darling dependencies, so it builds natively on Linux
 * for testing. Only 64-bit hosts are supported (struct input_event has the
 * 64-bit layout); elsewhere no device is ever found.
 *
 * Classification: a node is a keyboard if it has the Linux key codes of all
 * letters plus Enter and Space (so a power button or a media-key node is not
 * a keyboard); otherwise it is a mouse if it has relative X and Y motion and
 * BTN_LEFT. A node with both is a keyboard. Touch pads, tablets and touch
 * screens report absolute positions and are not mice.
 *
 * Keyboard report (no report ID, Keyboard/Keypad page 0x07): one byte of
 * modifiers (usages 0xE0..0xE7, bit 0 = left control), then one bit per
 * usage 0x04..0x73 (bit 0 of byte 1 = usage 0x04). Usages come from the
 * evdev key codes by position, for the keys of a standard 104/105 key
 * keyboard plus F13..F24. Keys without a usage are not reported. Auto-repeat
 * events are ignored.
 *
 * Mouse report (no report ID): button bits (Button page, usages 1..n, n up to
 * 5: left, right, middle, side=4, extra=5), padded to a byte; X and Y as
 * signed 16-bit little endian relative motion; the vertical wheel as a signed
 * byte, if the node has one; the horizontal wheel (Consumer AC Pan) as a
 * signed byte, if the node has one. Motion is the sum over one evdev event
 * frame (up to SYN_REPORT); the next frame starts from zero motion, and the
 * stored report keeps the last frame's motion until then.
 */

#ifndef IOKITD_HOST_INPUT_H
#define IOKITD_HOST_INPUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum iokitd_input_kind
{
	IOKITD_INPUT_KEYBOARD = 1,
	IOKITD_INPUT_MOUSE = 2,
};

#define IOKITD_INPUT_MAX_REPORT 32
#define IOKITD_KEYBOARD_REPORT_SIZE 15
#define IOKITD_KEYBOARD_FIRST_USAGE 0x04
#define IOKITD_KEYBOARD_LAST_USAGE 0x73

struct iokitd_input
{
	char path[64];
	char name[128];
	char serial[64];
	uint16_t bustype, vendor, product, version;

	int kind;             /* enum iokitd_input_kind */

	/* mouse */
	int button_count;     /* 1 .. 5 */
	int has_wheel, has_hwheel;
	uint16_t x_offset, y_offset, wheel_offset, hwheel_offset;

	uint16_t report_size; /* bytes */
};

/*
 * Reads the capabilities of an evdev node and decides whether it is a
 * keyboard or a mouse. Returns 0 and fills *dev, or -1 if the node cannot be
 * opened (no permission means there is simply no device), is neither, or is
 * not usable.
 */
int iokitd_input_probe(const char* path, struct iokitd_input* dev);

/* The evdev nodes under directory, as iokitd_gamepad_list() */
int iokitd_input_list(const char* directory, char paths[][64], int max);

/* Opens a node for reading events: non-blocking, close-on-exec */
int iokitd_input_open(const char* path);

/*
 * Reads all pending events from fd and applies them to report (dev->report_size
 * bytes, the current state). Returns 1 if a frame changed the report, 0 if
 * nothing relevant happened, -1 when the device is gone. As for game
 * controllers, a report is only updated at SYN_REPORT, and SYN_DROPPED
 * resynchronises from the kernel's state.
 *
 * Several frames can be pending at once, and for a mouse each frame carries
 * its own motion, so if frame is not NULL it is called with the report of
 * every frame that changed it, in order, before the next frame is applied.
 */
typedef void (*iokitd_input_frame_fn)(const uint8_t* report, void* context);
int iokitd_input_read(int fd, const struct iokitd_input* dev, uint8_t* report,
	iokitd_input_frame_fn frame, void* context);

/* The initial report: the keys and buttons held when the node is opened, no motion */
void iokitd_input_initial_report(int fd, const struct iokitd_input* dev, uint8_t* report);

/*
 * Applies one event to the report. Exposed for the layout tests;
 * iokitd_input_read() uses it. Returns 1 if the report changed.
 */
int iokitd_input_apply_event(const struct iokitd_input* dev, uint8_t* report,
	uint16_t type, uint16_t code, int32_t value);

/* Writes the HID report descriptor. Returns its length, or -1 if size is too small. */
int iokitd_input_report_descriptor(const struct iokitd_input* dev, uint8_t* out, size_t size);

/* The Keyboard page usage of an evdev key code, or 0 if it has none */
uint16_t iokitd_input_keyboard_usage(int code);

/* Generic Desktop usage of the device: 6 (Keyboard) or 2 (Mouse). */
static inline uint16_t iokitd_input_primary_usage(const struct iokitd_input* dev)
{
	return dev->kind == IOKITD_INPUT_KEYBOARD ? 6 : 2;
}

#ifdef __cplusplus
}
#endif

#endif
