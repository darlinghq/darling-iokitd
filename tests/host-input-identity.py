#!/usr/bin/env python3
"""Host test for the USB identity of host input devices (HostInputIdentity.c).

The LocationID follows the scheme macOS shows for USB devices: the bus
number in the top byte, then a nibble per hub tier. The strings and the
release number come from the USB device above an evdev node in sysfs; a node
that is not behind a USB device has none. Checked against a made-up sysfs
tree.

Usage: python3 tests/host-input-identity.py
"""
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "src")
if shutil.which("cc") is None:
    print("SKIP: cc is not installed")
    sys.exit(0)

work = tempfile.mkdtemp(prefix="input-identity-test.")
exe = os.path.join(work, "test")
subprocess.check_call(["cc", "-Wall", "-o", exe, os.path.join(here, "host-input-identity-test.c"), os.path.join(src, "HostInputIdentity.c")])

failed = False

def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed = failed or not ok

def run(*args):
    return subprocess.run([exe] + list(args), capture_output=True, text=True).stdout.strip()

# Location: a QEMU USB keyboard on port 2 of the bus 4 shows 0x04200000 on macOS 10.14
for bus, path, expected in (("4", "2", "04200000"), ("4", "1", "04100000"), ("1", "4.4.4.3.4", "01444340"),
                            ("3", "1.2.3.4.5.6", "03123456"), ("3", "1.2.3.4.5.6.7.8", "03123456"),
                            ("2", "20", "02f00000"), ("1", "x", "00000000"), ("1", "2..3", "00000000"), ("1", "", "00000000")):
    out = run("location", bus, path)
    check(out == expected, "LocationID of bus %s port chain '%s' is %s: %s" % (bus, path, expected, out))

def attr(path, name, value):
    with open(os.path.join(path, name), "w") as f:
        f.write(value + "\n")

root = os.path.join(work, "sys")
os.makedirs(os.path.join(root, "class", "input"))

def node(name, target):
    os.makedirs(target, exist_ok=True)
    os.symlink("..", os.path.join(target, "device"))   # the input device the node belongs to
    os.symlink(os.path.relpath(target, os.path.join(root, "class", "input")), os.path.join(root, "class", "input", name))

# event5: behind a hub: USB device 1-2.3 with all strings
usb = os.path.join(root, "devices", "pci0", "usb1", "1-2", "1-2.3")
os.makedirs(usb)
for name, value in (("idVendor", "046d"), ("manufacturer", "Logitech"), ("product", "Dual Action"), ("serial", "SERIAL1"),
                    ("bcdDevice", "0414"), ("busnum", "1"), ("devpath", "2.3")):
    attr(usb, name, value)
node("event5", os.path.join(usb, "1-2.3:1.0", "0003:046D:C216.0001", "input", "input7", "event5"))

# event6: a USB device without strings, bcdDevice 0
usb2 = os.path.join(root, "devices", "pci0", "usb1", "1-5")
os.makedirs(usb2)
for name, value in (("idVendor", "1234"), ("bcdDevice", "0000"), ("busnum", "1"), ("devpath", "5")):
    attr(usb2, name, value)
node("event6", os.path.join(usb2, "1-5:1.0", "0003:1234:5678.0002", "input", "input8", "event6"))

# event9: a virtual device
node("event9", os.path.join(root, "devices", "virtual", "input", "input9", "event9"))

out = run("identity", root, "event5")
check(out == "manufacturer=[Logitech] product=[Dual Action] serial=[SERIAL1] version=0414 location=01230000",
      "strings, bcdDevice and the location of a device behind a hub: " + out)
out = run("identity", root, "event6")
check(out == "manufacturer=[] product=[] serial=[] version=0000 location=01500000", "a USB device without strings, release 0: " + out)
out = run("identity", root, "event9")
check(out == "none", "a virtual device is not a USB device: " + out)
out = run("identity", root, "mouse0")
check(out == "none", "a node that is not an event node: " + out)

shutil.rmtree(work)
sys.exit(1 if failed else 0)
