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
 * Game controllers, keyboards and mice of the host that are HID devices of
 * the Linux HID subsystem, published with the report descriptor the device
 * itself has (HostHIDDescriptor.h) instead of one iokitd makes up.
 *
 * The descriptor is the one the kernel exposes in sysfs (report_descriptor
 * of the HID device behind the evdev node), which anyone can read. The values
 * come from the evdev node, which is what the user may open: the events of
 * the node are mapped back onto the fields of the real input reports by the
 * rules the kernel's HID input layer uses to make the events (usage to event
 * code: the axes X .. Rz, hats as ABS_HAT*, the buttons of the Button page
 * from BTN_JOYSTICK, BTN_GAMEPAD or BTN_MOUSE depending on the kind of
 * device, relative motion and wheels, the keys of the Keyboard page, ...).
 * A field whose usage has no event code the node offers (vendor-defined
 * fields, for one) stays at 0, except a hat, which then reads its null state.
 *
 * A hat follows the device's own range: ABS_HAT*X and Y become the value of
 * the field (the first direction, up, is the logical minimum, clockwise) and
 * the centre is a value outside the range, below the minimum when that is
 * above 0 and above the maximum when that fits the field.
 *
 * Like HostGamepad.c this file has no Darling dependencies and builds natively
 * for testing. The event reading is for 64-bit hosts only.
 */

#ifndef IOKITD_HOST_HID_DEVICE_H
#define IOKITD_HOST_HID_DEVICE_H

#include "HostHIDDescriptor.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOKITD_HID_KEY_BYTES 96   /* KEY_CNT / 8 */
#define IOKITD_HID_ABS_COUNT 64
#define IOKITD_HID_REL_COUNT 16

/* The event codes the node offers */
struct iokitd_hid_caps
{
	uint8_t keys[IOKITD_HID_KEY_BYTES];
	uint8_t abs[IOKITD_HID_ABS_COUNT / 8];
	uint8_t rel[IOKITD_HID_REL_COUNT / 8];
};

/* Reads the capabilities of an open evdev node. Returns 0, or -1. */
int iokitd_hid_caps_read(int fd, struct iokitd_hid_caps* caps);

struct iokitd_hid_device;

/*
 * Maps a parsed report descriptor onto the event codes of caps. Returns the
 * device, or NULL if the descriptor does not parse or none of its input
 * fields can be fed from the node (the caller then describes the device
 * another way).
 */
struct iokitd_hid_device* iokitd_hid_device_create(const uint8_t* descriptor, size_t size, const struct iokitd_hid_caps* caps);
void iokitd_hid_device_free(struct iokitd_hid_device* device);

const struct iokitd_hid_descriptor* iokitd_hid_device_descriptor(const struct iokitd_hid_device* device);

/* The input reports of the descriptor, in the order its reports list them */
size_t iokitd_hid_device_input_count(const struct iokitd_hid_device* device);
uint32_t iokitd_hid_device_input_id(const struct iokitd_hid_device* device, size_t index);
/* The current report, iokitd_hid_report_bytes() long, without the report ID */
const uint8_t* iokitd_hid_device_input(const struct iokitd_hid_device* device, size_t index, size_t* bytes);

/* Called with the report before and after for every input report a frame changed */
typedef void (*iokitd_hid_report_fn)(size_t index, uint32_t id, const uint8_t* previous, const uint8_t* current, size_t bytes, void* context);

/*
 * Applies one event to the state. Exposed for the tests; the reading
 * functions use it. Returns 1 if the state changed.
 */
int iokitd_hid_device_apply(struct iokitd_hid_device* device, uint16_t type, uint16_t code, int32_t value);

/*
 * Ends a frame (SYN_REPORT): builds the reports from the state and calls fn
 * for those that changed. Relative motion is the sum over the frame; the
 * motion of the last frame stays in the reports until the next frame starts.
 * Returns how many reports changed.
 */
int iokitd_hid_device_commit(struct iokitd_hid_device* device, iokitd_hid_report_fn fn, void* context);

/* Reads the state of an open node (held keys, axis positions) into the reports, without calling anyone */
void iokitd_hid_device_initial(struct iokitd_hid_device* device, int fd);

/* Reads the pending events of fd. Returns 1 if a report changed, 0 if not, -1 when the node is gone. */
int iokitd_hid_device_read(struct iokitd_hid_device* device, int fd, iokitd_hid_report_fn fn, void* context);

/*
 * The keyboard LEDs an output report sets: if the report with the ID has the
 * LED page usages Num Lock .. Kana as fields, sets bit n of *leds for usage
 * n + 1 when the field is nonzero in data (the report without its ID, shorter
 * data counts as zeros) and returns 1; returns 0 if the report has no LED field.
 */
int iokitd_hid_output_leds(const struct iokitd_hid_descriptor* descriptor, uint32_t id, const uint8_t* data, size_t length, uint8_t* leds);

/*
 * The HID device behind an evdev node: finds the directory that holds the
 * report descriptor above /<sysroot>/class/input/<event name> and reads the
 * descriptor. sysroot is /sys, or where the host's sysfs is mounted for the
 * caller. Returns 0 with *descriptor (malloc'ed) and *size, and the sysfs
 * directory of the HID device in hid_dir (size hid_dir_size); -1 if the node
 * has no HID device behind it (made by uinput, a platform driver, ...).
 */
int iokitd_hid_sysfs_descriptor(const char* sysroot, const char* event_name, uint8_t** descriptor, size_t* size,
	char* hid_dir, size_t hid_dir_size);

/* The names of the event nodes of the HID device in hid_dir (each at most 31 characters), at most max. */
int iokitd_hid_sysfs_siblings(const char* hid_dir, char names[][32], int max);

/* Keyboard page usage -> evdev key code, or 0 */
int iokitd_hid_key_code(uint32_t usage);

#ifdef __cplusplus
}
#endif

#endif
