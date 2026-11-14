#!/usr/bin/env python3
# Copyright (C) 2026 Lubos Dolezel
#
# Host side of the guest test iohid-input-services --virtual: creates a
# virtual keyboard and mouse through uinput (needs write access to
# /dev/uinput), keeps them for the given number of seconds (default 30) and
# removes them. Usage: python3 uinput-input.py [seconds]
import fcntl, os, struct, sys, time

def _IOC(d, nr, size): return (d << 30) | (size << 16) | (ord('U') << 8) | nr
UI_DEV_CREATE, UI_DEV_DESTROY = _IOC(0, 1, 0), _IOC(0, 2, 0)
UI_DEV_SETUP = _IOC(1, 3, 92)
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_RELBIT = _IOC(1, 100, 4), _IOC(1, 101, 4), _IOC(1, 102, 4)

def create(name, keys, rels):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, 1)
    if rels:
        fcntl.ioctl(fd, UI_SET_EVBIT, 2)
    for k in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    for r in rels:
        fcntl.ioctl(fd, UI_SET_RELBIT, r)
    fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack("HHHH80sI", 0x03, 0x1234, 0x5678, 1, name.encode(), 0))
    fcntl.ioctl(fd, UI_DEV_CREATE)
    return fd

letters = list(range(16, 26)) + list(range(30, 39)) + list(range(44, 51))
kbd = create("Darling Test Keyboard", letters + [28, 57, 42], [])
mouse = create("Darling Test Mouse", [0x110, 0x111, 0x112], [0, 1, 8])
print("devices created; waiting")
sys.stdout.flush()
time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 30)
for fd in (kbd, mouse):
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    os.close(fd)
