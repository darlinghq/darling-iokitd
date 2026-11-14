#!/usr/bin/env python3
"""Host test for iokitd's keyboard and mouse support (HostInput.c).

HostInput.c is built natively together with tests/host-input-test.c.

Always checked ("layout"): the HID report descriptors generated for a
described keyboard and mouse, the key usages, and the folding of evdev
events into their input reports.

Checked when /dev/uinput can be written (usually root, or a udev rule): a
virtual keyboard and mouse made through uinput are recognised (and a node
with a single key, and a node with absolute axes and a button, are not), the
initial state is read, and key, button and motion events change the report
in the expected bytes.

Usage: python3 tests/host-input.py
"""
import fcntl, os, shutil, struct, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
if shutil.which("cc") is None:
    print("SKIP: cc is not installed")
    sys.exit(0)

work = tempfile.mkdtemp(prefix="input-test.")
exe = os.path.join(work, "test")
subprocess.check_call(["cc", "-Wall", "-o", exe, os.path.join(here, "host-input-test.c"),
                       os.path.join(here, "..", "src", "HostInput.c"),
                       os.path.join(here, "..", "src", "HostGamepad.c")])

failed = False

def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed = failed or not ok

out = subprocess.run([exe, "layout"], capture_output=True, text=True)
check(out.returncode == 0 and "layout: ok" in out.stdout, "descriptors and event folding\n" + out.stdout)

# --- uinput ---------------------------------------------------------------
def _IOC(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr
UI_DEV_CREATE = _IOC(0, 'U', 1, 0)
UI_DEV_DESTROY = _IOC(0, 'U', 2, 0)
UI_DEV_SETUP = _IOC(1, 'U', 3, 92)
UI_ABS_SETUP = _IOC(1, 'U', 4, 28)
UI_SET_EVBIT = _IOC(1, 'U', 100, 4)
UI_SET_KEYBIT = _IOC(1, 'U', 101, 4)
UI_SET_RELBIT = _IOC(1, 'U', 102, 4)
UI_SET_ABSBIT = _IOC(1, 'U', 103, 4)
UI_GET_SYSNAME = lambda n: _IOC(2, 'U', 44, n)
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3

def create(name, keys=(), rels=(), absaxes=()):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    if rels:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_REL)
    if absaxes:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
    for k in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    for r in rels:
        fcntl.ioctl(fd, UI_SET_RELBIT, r)
    for code, mn, mx in absaxes:
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
    LETTERS = list(range(16, 26)) + list(range(30, 39)) + list(range(44, 51))
    KEY_A, KEY_LEFTSHIFT, KEY_ENTER, KEY_SPACE = 30, 42, 28, 57
    BTN_LEFT, BTN_RIGHT = 0x110, 0x111
    REL_X, REL_Y, REL_WHEEL = 0, 1, 8
    created = []

    def make(*a, **k):
        fd, node = create(*a, **k)
        created.append(fd)
        return fd, node

    def probe(node):
        return subprocess.run([exe, "probe", node], capture_output=True, text=True).stdout.strip()

    try:
        kfd, knode = make("Darling Test Keyboard", keys=LETTERS + [KEY_ENTER, KEY_SPACE, KEY_LEFTSHIFT])
        mfd, mnode = make("Darling Test Mouse", keys=[BTN_LEFT, BTN_RIGHT], rels=[REL_X, REL_Y, REL_WHEEL])
        pfd, pnode = make("Darling Test Power Button", keys=[116])
        tfd, tnode = make("Darling Test Touch Pad", keys=[BTN_LEFT], absaxes=[(0, 0, 100), (1, 0, 100)])
        if not (knode and mnode and os.access(knode, os.R_OK) and os.access(mnode, os.R_OK)):
            print("SKIP: no permission to read the virtual devices' event nodes")
        else:
            check(probe(knode) == "keyboard Darling Test Keyboard|1234|5678|15", "keyboard recognised: " + probe(knode))
            check(probe(mnode) == "mouse Darling Test Mouse|1234|5678|6", "mouse recognised: " + probe(mnode))
            if pnode and os.access(pnode, os.R_OK):
                check(probe(pnode) == "none", "a node with one key is not a keyboard: " + probe(pnode))
            if tnode and os.access(tnode, os.R_OK):
                check(probe(tnode) == "none", "a node with absolute axes is not a mouse: " + probe(tnode))

            watch = subprocess.Popen([exe, "watch", knode, "3"], stdout=subprocess.PIPE, text=True)
            first = watch.stdout.readline().strip()
            check(first == "report " + " ".join(["00"] * 15), "keyboard initial report: " + first)
            emit(kfd, [(EV_KEY, KEY_LEFTSHIFT, 1)])
            emit(kfd, [(EV_KEY, KEY_A, 1)])
            emit(kfd, [(EV_KEY, KEY_A, 0), (EV_KEY, KEY_LEFTSHIFT, 0)])
            lines = [watch.stdout.readline().strip() for _ in range(3)]
            watch.wait(timeout=10)
            zeros = ["00"] * 14
            expected = ["report 02 " + " ".join(["00"] * 14),
                        "report 02 01 " + " ".join(["00"] * 13),
                        "report 00 " + " ".join(zeros)]
            check(lines == expected, "modifier and key change the report: %r" % lines)

            watch = subprocess.Popen([exe, "watch", mnode, "3"], stdout=subprocess.PIPE, text=True)
            first = watch.stdout.readline().strip()
            check(first == "report 00 00 00 00 00 00", "mouse initial report: " + first)
            # sent back to back, so the watcher may read several frames at once
            emit(mfd, [(EV_KEY, BTN_RIGHT, 1)])
            emit(mfd, [(EV_REL, REL_X, 5), (EV_REL, REL_Y, -2), (EV_REL, REL_X, 1)])
            emit(mfd, [(EV_REL, REL_WHEEL, 1)])
            lines = [watch.stdout.readline().strip() for _ in range(3)]
            watch.wait(timeout=10)
            expected = ["report 02 00 00 00 00 00",
                        "report 02 06 00 fe ff 00",
                        "report 02 00 00 00 00 01"]
            check(lines == expected, "button, motion and wheel change the report: %r" % lines)
    finally:
        for f in created:
            try:
                fcntl.ioctl(f, UI_DEV_DESTROY)
            except OSError:
                pass
            os.close(f)

shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if failed else 0)
