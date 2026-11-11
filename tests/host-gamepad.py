#!/usr/bin/env python3
"""Host test for iokitd's game controller support (HostGamepad.c).

HostGamepad.c is built natively together with tests/host-gamepad-test.c.

Always checked ("layout"): the HID report descriptor generated for a
described controller, and the folding of evdev events into its input report
(buttons, the hat from its two axes, signed and unsigned axes, clamping).

Checked when /dev/uinput can be written (usually root, or a udev rule): a
virtual controller made through uinput is recognised as a controller (and
a virtual keyboard-like device is not), its initial state is read, and button,
hat and axis events change the report in the expected bytes.

Usage: python3 tests/host-gamepad.py
"""
import fcntl, os, shutil, struct, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
if shutil.which("cc") is None:
    print("SKIP: cc is not installed")
    sys.exit(0)

work = tempfile.mkdtemp(prefix="gamepad-test.")
exe = os.path.join(work, "test")
subprocess.check_call(["cc", "-Wall", "-o", exe, os.path.join(here, "host-gamepad-test.c"),
                       os.path.join(here, "..", "src", "HostGamepad.c")])

failed = False

def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed = failed or not ok

out = subprocess.run([exe, "layout"], capture_output=True, text=True)
check(out.returncode == 0 and "layout: ok" in out.stdout, "descriptor and event folding\n" + out.stdout)

# --- uinput ---------------------------------------------------------------
def _IOC(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr
UI_DEV_CREATE = _IOC(0, 'U', 1, 0)
UI_DEV_DESTROY = _IOC(0, 'U', 2, 0)
UI_DEV_SETUP = _IOC(1, 'U', 3, 92)
UI_ABS_SETUP = _IOC(1, 'U', 4, 28)
UI_SET_EVBIT = _IOC(1, 'U', 100, 4)
UI_SET_KEYBIT = _IOC(1, 'U', 101, 4)
UI_SET_ABSBIT = _IOC(1, 'U', 103, 4)
UI_GET_SYSNAME = lambda n: _IOC(2, 'U', 44, n)
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3

def create(name, keys, axes):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
    for k in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    for code, mn, mx in axes:
        fcntl.ioctl(fd, UI_SET_ABSBIT, code)
        fcntl.ioctl(fd, UI_ABS_SETUP, struct.pack("H2xiiiiii", code, 0, mn, mx, 0, 0, 0))
    setup = struct.pack("HHHH80sI", 0x03, 0x1234, 0x5678, 1, name.encode(), 0)
    fcntl.ioctl(fd, UI_DEV_SETUP, setup)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    buf = bytearray(64)
    fcntl.ioctl(fd, UI_GET_SYSNAME(64), buf)
    sysname = bytes(buf).split(b"\0")[0].decode()
    node = None
    for _ in range(100):
        d = "/sys/devices/virtual/input/" + sysname
        if os.path.isdir(d):
            for e in os.listdir(d):
                if e.startswith("event") and os.path.exists("/dev/input/" + e):
                    node = "/dev/input/" + e
        if node and os.access(node, os.R_OK):
            break
        time.sleep(0.05)
    return fd, node

def emit(fd, events):
    for t, c, v in events:
        os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))
    os.write(fd, struct.pack("llHHi", 0, 0, EV_SYN, 0, 0))

if not os.access("/dev/uinput", os.W_OK):
    print("SKIP: /dev/uinput is not writable; only the layout test ran")
else:
    BTN_SOUTH, BTN_EAST, BTN_DPAD_UP = 0x130, 0x131, 0x220
    fd, node = create("Darling Test Pad", [BTN_SOUTH, BTN_EAST],
                      [(0, -32768, 32767), (1, -32768, 32767), (2, 0, 255), (0x10, -1, 1), (0x11, -1, 1)])
    other_fd, other = create("Darling Test Mouse-like", [0x110], [(0, 0, 100), (1, 0, 100)])
    try:
        if not node or not os.access(node, os.R_OK):
            print("SKIP: no permission to read the virtual device's event node")
        else:
            probe = subprocess.run([exe, "probe", node], capture_output=True, text=True).stdout.strip()
            check(probe == "pad Darling Test Pad|1234|5678|2|3|1|8", "controller recognised: " + probe)
            if other and os.access(other, os.R_OK):
                probe = subprocess.run([exe, "probe", other], capture_output=True, text=True).stdout.strip()
                check(probe == "none", "a device with no controller buttons is not a controller: " + probe)

            watch = subprocess.Popen([exe, "watch", node, "4"], stdout=subprocess.PIPE, text=True)
            first = watch.stdout.readline().strip()
            check(first == "report 00 00 00 00 00 00 00 00", "initial report: " + first)
            emit(fd, [(EV_KEY, BTN_EAST, 1)])
            emit(fd, [(EV_ABS, 0x11, -1)])
            emit(fd, [(EV_ABS, 0, 1)])
            emit(fd, [(EV_ABS, 2, 200)])
            lines = [watch.stdout.readline().strip() for _ in range(4)]
            watch.wait(timeout=10)
            expected = ["report 02 00 00 00 00 00 00 00",
                        "report 02 01 00 00 00 00 00 00",
                        "report 02 01 01 00 00 00 00 00",
                        "report 02 01 01 00 00 00 c8 00"]
            check(lines == expected, "button, hat and axes change the report: %r" % lines)
    finally:
        for f in (fd, other_fd):
            try:
                fcntl.ioctl(f, UI_DEV_DESTROY)
            except OSError:
                pass
            os.close(f)

shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if failed else 0)
