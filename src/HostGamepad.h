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
 * Mapping to HID. The descriptor follows the layout that real controllers
 * of the same kind publish, so that consumers which expect it (device
 * databases keyed on usages, element order, button numbers) find it:
 *  - A controller with the BTN_GAMEPAD block (BTN_SOUTH .. BTN_THUMBR) uses
 *    the standard layout, or the PlayStation layout for the DualShock 4
 *    (Sony vendor id, product ids 0x05c4, 0x09cc, 0x0ba0). Anything else is
 *    a joystick (Generic Desktop Joystick, usage 4) with the joystick layout.
 *  - Button usages are contiguous, 1 .. N, and the report holds exactly N
 *    bits for them (a bit stays 0 for a button the controller lacks).
 *    Standard layout: the Linux code BTN_GAMEPAD + k is usage k + 1 (A is
 *    1, B 2, X 4, Y 5, LB 7, RB 8, View 11, Menu 12, Guide 13, the stick
 *    buttons 14 and 15; 15 usages, as the Xbox One controller). PlayStation
 *    layout: Square 1, Cross 2, Circle 3, Triangle 4, L1 5, R1 6, L2 7,
 *    R2 8, Share 9, Options 10, L3 11, R3 12, PS 13, touch pad 14 (14
 *    usages). Joystick layout: BTN_JOYSTICK + k is usage k + 1. Buttons the
 *    layout has no usage for (BTN_DPAD_*, BTN_TRIGGER_HAPPY, ...) follow in
 *    the order of their codes.
 *  - Sticks and triggers follow the Linux gamepad convention (ABS_X, ABS_Y
 *    left stick, ABS_RX, ABS_RY right stick, ABS_Z, ABS_RZ the triggers).
 *    A gamepad without both ABS_RX and ABS_RY has ABS_Z, ABS_RZ as its
 *    right stick instead (and no analog triggers). Gamepad layouts publish the left stick as X, Y
 *    and the right stick as Z, Rz (usages 0x30, 0x31, 0x32, 0x35). The
 *    triggers are Brake (the left, Simulation Controls 0xC5) and Accelerator
 *    (the right, 0xC4) in the standard layout and Rx, Ry (0x33, 0x34) in
 *    the PlayStation layout. A joystick publishes ABS_X .. ABS_RZ as X, Y,
 *    Z, Rx, Ry, Rz. ABS_THROTTLE, ABS_RUDDER, ABS_GAS and ABS_BRAKE are
 *    Throttle (0xBB), Rudder (0xBA), Accelerator and Brake.
 *  - Axis values are reported as the offset from the minimum of the
 *    controller's range: logical minimum 0, maximum the size of the range.
 *    The field is 8 bits for a range up to 255, 10 bits up to 1023 (padded to
 *    16), 16 bits up to 65535, else 32.
 *  - ABS_HAT0X and ABS_HAT0Y together are one Hat switch (0x39), 4 bits,
 *    degrees, physical 0 .. 315, null state when centred. Standard and
 *    joystick layout: logical 1 .. 8 (up is 1, clockwise), null state 0.
 *    PlayStation layout: logical 0 .. 7 (up is 0), null state 8.
 * The input report has no report ID, unless the controller can rumble (see
 * below). Fields follow in this order, each
 * padded to a byte boundary except the PlayStation hat (the buttons share
 * its byte): the axes (the PlayStation layout puts its triggers after the
 * buttons), the hat, the buttons.
 *
 * Rumble. A controller with the evdev force-feedback effect FF_RUMBLE also
 * publishes the output report that controllers of its layout have, and its
 * input report then carries report ID 1, as those controllers' do:
 *  - Standard layout: output report 3 of the Xbox One controller, 8 bytes:
 *    byte 0 bits 0 .. 3 enable the actuators (bit 0 the weak motor, bit 1
 *    the strong motor, 2 and 3 the triggers), bytes 1 .. 4 the magnitudes
 *    0 .. 100 (index 2 the strong motor, 3 the weak motor, 0 and 1 the
 *    trigger motors), byte 5 the duration and byte 6 the start delay in
 *    10 ms units, byte 7 the loop count.
 *  - PlayStation layout: output report 5 (31 bytes, 77 bytes as report 17
 *    for a Bluetooth controller) of the DualShock 4: bit 0 of the first
 *    flags byte enables rumble, followed by the weak and the strong motor
 *    strength 0 .. 255. Other fields (light bar) are ignored.
 * The motors without an evdev counterpart (triggers) are ignored. The
 * output report 0 magnitudes drive one FF_RUMBLE effect of the node.
 */

#ifndef IOKITD_HOST_GAMEPAD_H
#define IOKITD_HOST_GAMEPAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOKITD_GAMEPAD_MAX_BUTTONS 48
#define IOKITD_GAMEPAD_MAX_AXES 16
#define IOKITD_GAMEPAD_MAX_FIELDS (IOKITD_GAMEPAD_MAX_AXES + 2)
#define IOKITD_GAMEPAD_MAX_REPORT 128

enum iokitd_gamepad_layout
{
	IOKITD_GAMEPAD_LAYOUT_JOYSTICK,
	IOKITD_GAMEPAD_LAYOUT_STANDARD,
	IOKITD_GAMEPAD_LAYOUT_PLAYSTATION,
};

enum iokitd_gamepad_output
{
	IOKITD_GAMEPAD_OUTPUT_NONE,
	IOKITD_GAMEPAD_OUTPUT_XBOX,        /* report 3 */
	IOKITD_GAMEPAD_OUTPUT_DUALSHOCK,   /* report 5, or 17 on Bluetooth */
};

enum iokitd_gamepad_field_kind
{
	IOKITD_GAMEPAD_FIELD_AXIS,
	IOKITD_GAMEPAD_FIELD_HAT,
	IOKITD_GAMEPAD_FIELD_BUTTONS,
};

struct iokitd_gamepad_button
{
	uint16_t code;       /* evdev key code */
	uint16_t usage;      /* Button page usage; the report bit is usage - 1 */
};

struct iokitd_gamepad_axis
{
	uint16_t code;       /* evdev axis code */
	uint16_t usage_page;
	uint16_t usage;
	int32_t minimum, maximum; /* evdev range; the report holds value - minimum */
	uint8_t bits;        /* 8, 10, 16 or 32 */
};

/* One input item of the report, in report order */
struct iokitd_gamepad_field
{
	uint8_t kind;        /* enum iokitd_gamepad_field_kind */
	uint8_t index;       /* into axes[] for an axis */
	uint16_t bit_offset; /* in the input report */
	uint8_t bits;        /* size of the item (for buttons: button_bits) */
	uint8_t padding;     /* constant bits that follow the item */
};

struct iokitd_gamepad
{
	char path[64];
	char name[128];
	char serial[64];
	uint16_t bustype, vendor, product, version;

	int has_gamepad_buttons; /* the BTN_GAMEPAD block, as opposed to a joystick */
	int layout;              /* enum iokitd_gamepad_layout */

	int button_count;        /* buttons the controller has */
	struct iokitd_gamepad_button buttons[IOKITD_GAMEPAD_MAX_BUTTONS];
	int button_bits;         /* usages 1 .. button_bits are declared (0 if no buttons) */

	int axis_count;          /* without the hat */
	struct iokitd_gamepad_axis axes[IOKITD_GAMEPAD_MAX_AXES];

	int has_hat;
	int32_t hat_x_min, hat_x_max, hat_y_min, hat_y_max;
	int hat_logical_min;     /* 1 (null state 0) or 0 (null state 8); the maximum is min + 7 */

	int field_count;
	struct iokitd_gamepad_field fields[IOKITD_GAMEPAD_MAX_FIELDS];

	uint16_t report_size;    /* bytes, without the report ID */

	int has_rumble;          /* FF_RUMBLE */
	uint8_t input_report_id; /* 0, or 1 for a controller that can rumble */
	uint8_t output_kind;     /* enum iokitd_gamepad_output */
	uint8_t output_report_id;
	uint16_t output_size;    /* bytes, without the report ID */
};

/* What an output report asks of the rumble motors */
struct iokitd_rumble
{
	uint16_t strong, weak;   /* evdev magnitudes 0 .. 65535 */
	uint16_t length_ms;      /* 0: until stopped */
	uint16_t delay_ms;
};

/*
 * Reads the rumble part of an output report (data and length without the
 * report ID; shorter data is padded with zeros). Returns 1 and fills *out
 * (all zero when the report enables no motor of an Xbox One controller), 0 if
 * the report does not address the motors (a DualShock 4 report without the
 * rumble flag), or -1 if it is not a report of this controller.
 */
int iokitd_gamepad_decode_output(const struct iokitd_gamepad* pad, const uint8_t* data, size_t length,
	struct iokitd_rumble* out);

/*
 * Plays the effect on the node's fd, which must be open for writing: the
 * first call uploads an effect (*effect_id is -1 before, the kernel's id
 * after), later calls update it. All-zero strengths stop it. Returns 0, or -1
 * with errno set (EBADF when fd is read-only).
 */
int iokitd_gamepad_rumble(int fd, int* effect_id, const struct iokitd_rumble* rumble);

/*
 * Fixes the report layout (the fields, their offsets and report_size) once
 * layout, buttons, button_bits, axes, has_hat and hat_logical_min are set.
 * iokitd_gamepad_probe() calls it; exposed for the descriptor tests.
 */
void iokitd_gamepad_layout(struct iokitd_gamepad* pad);

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
