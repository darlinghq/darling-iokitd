#!/usr/bin/env python3
"""Host test for iokitd's HID report descriptor support (HostHIDDescriptor.c,
HostHIDDevice.c), built natively together with tests/host-hid-descriptor-test.c.

Checked without any device: the elements the parser makes for descriptors
(what IOHIDManager showed on macOS 10.14.4 for a USB tablet and keyboard,
and the descriptors of a game controller and of a device with report IDs),
malformed descriptors, and the mapping of evdev events onto the fields of the
real reports (axes, hat, buttons, relative motion, key arrays).
Also checked: finding the HID device and its descriptor behind an evdev node
in a made-up sysfs tree.

Usage: python3 tests/host-hid-descriptor.py
"""
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "src")
if shutil.which("cc") is None:
    print("SKIP: cc is not installed")
    sys.exit(0)

work = tempfile.mkdtemp(prefix="hid-descriptor-test.")
exe = os.path.join(work, "test")
subprocess.check_call(["cc", "-Wall", "-std=gnu11", "-o", exe, os.path.join(here, "host-hid-descriptor-test.c"),
                       os.path.join(src, "HostHIDDescriptor.c"), os.path.join(src, "HostHIDDevice.c"),
                       os.path.join(src, "HostInput.c"), os.path.join(src, "HostGamepad.c")])

failed = False

def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed = failed or not ok

out = subprocess.run([exe], capture_output=True, text=True)
check(out.returncode == 0 and "descriptor: ok" in out.stdout, "elements, malformed descriptors and event mapping\n" + out.stdout)

# A made-up sysfs: event5 is a node of the HID device hid1, which has a second node event6;
# event9 belongs to a device that has no report descriptor (a virtual one)
root = os.path.join(work, "sys")
hid = os.path.join(root, "devices", "pci0", "usb1", "1-2", "1-2:1.0", "0003:1234:5678.0001")
for name, inp in (("event5", "input3"), ("event6", "input4")):
    d = os.path.join(hid, "input", inp, name)
    os.makedirs(d)
virt = os.path.join(root, "devices", "virtual", "input", "input9", "event9")
os.makedirs(virt)
with open(os.path.join(hid, "report_descriptor"), "wb") as f:
    f.write(bytes([0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0xc0]))
os.makedirs(os.path.join(root, "class", "input"))
for name, target in (("event5", os.path.join(hid, "input", "input3", "event5")),
                     ("event6", os.path.join(hid, "input", "input4", "event6")),
                     ("event9", virt)):
    os.symlink(os.path.relpath(target, os.path.join(root, "class", "input")), os.path.join(root, "class", "input", name))

out = subprocess.run([exe, "sysfs", root, "event5"], capture_output=True, text=True).stdout.strip()
check(out.startswith("descriptor 7 bytes first 05 siblings") and "event5" in out and "event6" in out, "descriptor and sibling nodes behind an evdev node: " + out)
out = subprocess.run([exe, "sysfs", root, "event9"], capture_output=True, text=True).stdout.strip()
check(out == "none", "a node without a HID device behind it has no descriptor: " + out)
out = subprocess.run([exe, "sysfs", root, "event1/../event5"], capture_output=True, text=True).stdout.strip()
check(out == "none", "a node name that is not eventN is refused: " + out)

shutil.rmtree(work)
sys.exit(1 if failed else 0)
