#!/usr/bin/env python3
"""Host test for iokitd's D-Bus power integration (HostBus.c, LogindSleep.c,
HostPowerInhibit.c).

The three C files are built natively against the host's libdbus-1. Two
private dbus-daemons stand in for the system and the session bus, with a fake
systemd-logind and a fake org.freedesktop.ScreenSaver on them. Neither the
machine's buses nor its logind are used, so nothing is inhibited and the
machine is never suspended.

Checked:
- the sleep monitor holds a "sleep" "delay" inhibitor; PrepareForSleep(true)
  reaches will_sleep, and the inhibitor is released when iokitd says the
  applications are ready, or when the acknowledgement timeout expires;
  PrepareForSleep(false) reaches did_wake and the inhibitor is taken again;
  a logind restart is followed by a new inhibitor; stopping releases it;
- idle assertions hold one "idle" "block" inhibitor however many are taken;
- display assertions hold one ScreenSaver inhibition, released by UnInhibit;
- without buses everything fails fast;
- bus addresses from the environment are restricted to a single plain unix
  socket.

Usage: python3 tests/host-power-dbus.py [source dir]
"""
import os, shutil, signal, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src"))

for tool in ("cc", "pkg-config", "dbus-daemon"):
    if shutil.which(tool) is None:
        print("SKIP: %s is not installed" % tool)
        sys.exit(0)
try:
    import dbus  # noqa: F401
    from gi.repository import GLib  # noqa: F401
except ImportError:
    print("SKIP: python3-dbus or python3-gi is not installed")
    sys.exit(0)

FAKE = r'''
import os, sys, dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
system = dbus.bus.BusConnection(sys.argv[1])
session = dbus.bus.BusConnection(sys.argv[2])
LOGIND = "org.freedesktop.login1"

inhibitors = []   # [what, mode, read end]
cookies = set()
next_cookie = [1]

class Manager(dbus.service.Object):
    @dbus.service.method("org.freedesktop.login1.Manager", in_signature="ssss", out_signature="h")
    def Inhibit(self, what, who, why, mode):
        r, w = os.pipe()
        entry = [str(what), str(mode), r]
        inhibitors.append(entry)
        def closed(fd, cond):
            inhibitors.remove(entry)
            os.close(fd)
            return False
        GLib.io_add_watch(r, GLib.IO_HUP | GLib.IO_ERR, closed)
        fd = dbus.types.UnixFd(w)
        os.close(w)
        return fd

    @dbus.service.signal("org.freedesktop.login1.Manager", signature="b")
    def PrepareForSleep(self, start):
        pass

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
    def Get(self, interface, name):
        if name == "InhibitDelayMaxUSec":
            return dbus.UInt64(1500000, variant_level=1)
        raise dbus.exceptions.DBusException("unknown property", name="org.freedesktop.DBus.Error.UnknownProperty")

class ScreenSaver(dbus.service.Object):
    @dbus.service.method("org.freedesktop.ScreenSaver", in_signature="ss", out_signature="u")
    def Inhibit(self, app, reason):
        cookie = next_cookie[0]
        next_cookie[0] += 1
        cookies.add(cookie)
        return dbus.UInt32(cookie)

    @dbus.service.method("org.freedesktop.ScreenSaver", in_signature="u", out_signature="")
    def UnInhibit(self, cookie):
        cookies.discard(int(cookie))

manager = Manager(system, "/org/freedesktop/login1")
ScreenSaver(session, "/org/freedesktop/ScreenSaver")

class Control(dbus.service.Object):
    @dbus.service.method("org.darlinghq.Test", in_signature="ss", out_signature="u")
    def Count(self, what, mode):
        return dbus.UInt32(sum(1 for e in inhibitors if e[0] == what and e[1] == mode))

    @dbus.service.method("org.darlinghq.Test", in_signature="", out_signature="u")
    def Cookies(self):
        return dbus.UInt32(len(cookies))

    @dbus.service.method("org.darlinghq.Test", in_signature="b", out_signature="")
    def Emit(self, start):
        manager.PrepareForSleep(start)

    @dbus.service.method("org.darlinghq.Test", in_signature="", out_signature="")
    def Restart(self):
        system.release_name(LOGIND)
        GLib.timeout_add(100, lambda: system.request_name(LOGIND) and False)

Control(system, "/test")
system.request_name("org.darlinghq.Test")
system.request_name(LOGIND)
session.request_name("org.freedesktop.ScreenSaver")
print("ready", flush=True)
GLib.MainLoop().run()
'''

DRIVER = r'''
#include "HostBus.h"
#include "HostPowerInhibit.h"
#include "LogindSleep.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static DBusConnection* control;
static volatile int slept, woke;
static volatile unsigned long long last_cycle;

static void will_sleep(void* ctx, uint64_t cycle) { (void) ctx; last_cycle = cycle; slept++; }
static void did_wake(void* ctx, uint64_t cycle) { (void) ctx; (void) cycle; woke++; }

static unsigned call_u(const char* method, const char* a, const char* b)
{
	DBusMessage* m = dbus_message_new_method_call("org.darlinghq.Test", "/test", "org.darlinghq.Test", method);
	DBusMessageIter it;
	dbus_uint32_t v = 0xffffffff;
	if (a)
	{
		dbus_message_iter_init_append(m, &it);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &a);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &b);
	}
	DBusMessage* r = dbus_connection_send_with_reply_and_block(control, m, 2000, NULL);
	dbus_message_unref(m);
	if (r && dbus_message_iter_init(r, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32)
		dbus_message_iter_get_basic(&it, &v);
	if (r)
		dbus_message_unref(r);
	return v;
}

static void call_void(const char* method, int has_bool, dbus_bool_t value)
{
	DBusMessage* m = dbus_message_new_method_call("org.darlinghq.Test", "/test", "org.darlinghq.Test", method);
	DBusMessageIter it;
	if (has_bool)
	{
		dbus_message_iter_init_append(m, &it);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &value);
	}
	DBusMessage* r = dbus_connection_send_with_reply_and_block(control, m, 2000, NULL);
	dbus_message_unref(m);
	if (r)
		dbus_message_unref(r);
}

static unsigned count(const char* what, const char* mode) { return call_u("Count", what, mode); }

/* Waits up to 3 s for cond to hold. */
#define WAIT_FOR(cond) do { for (int i_ = 0; i_ < 300 && !(cond); i_++) usleep(10000); } while (0)

static void check_address(const char* var, const char* value, enum iokitd_host_bus bus, int ok)
{
	char buffer[256];
	setenv(var, value, 1);
	int r = iokitd_host_bus_address(bus, buffer, sizeof(buffer));
	CHECK((r == 0) == ok, "%s=%s: %d", var, value, r);
}

int main(int argc, char** argv)
{
	alarm(60);
	const char* system_address = argv[1];
	const char* session_address = argv[2];
	setenv("DBUS_SYSTEM_BUS_ADDRESS", system_address, 1);
	setenv("DBUS_SESSION_BUS_ADDRESS", session_address, 1);

	control = iokitd_host_bus_open(IOKITD_SYSTEM_BUS);
	CHECK(control != NULL, "control connection");
	if (!control)
		return 1;

	/* Sleep monitor */
	static const struct iokitd_logind_callbacks callbacks = { will_sleep, did_wake, NULL };
	struct iokitd_logind* l = iokitd_logind_start(&callbacks, NULL, 0);
	CHECK(l != NULL, "monitor started");
	CHECK(iokitd_logind_ack_timeout_usec(l) == 750000, "ack timeout %llu from InhibitDelayMaxUSec 1.5 s",
		(unsigned long long) iokitd_logind_ack_timeout_usec(l));
	CHECK(count("sleep", "delay") == 1, "delay inhibitor at start: %u", count("sleep", "delay"));

	call_void("Emit", 1, 1);
	WAIT_FOR(slept == 1);
	CHECK(slept == 1, "will_sleep called");
	CHECK(count("sleep", "delay") == 1, "inhibitor kept until the applications are ready");
	iokitd_logind_sleep_ready(l, last_cycle + 1);
	usleep(100000);
	CHECK(count("sleep", "delay") == 1, "readiness for another cycle ignored");
	iokitd_logind_sleep_ready(l, last_cycle);
	WAIT_FOR(count("sleep", "delay") == 0);
	CHECK(count("sleep", "delay") == 0, "inhibitor released when ready");

	call_void("Emit", 1, 0);
	WAIT_FOR(woke == 1 && count("sleep", "delay") == 1);
	CHECK(woke == 1, "did_wake called");
	CHECK(count("sleep", "delay") == 1, "inhibitor taken again after resume");

	/* Nobody acknowledges: the timeout releases the inhibitor. */
	call_void("Emit", 1, 1);
	WAIT_FOR(slept == 2);
	usleep(200000);
	CHECK(count("sleep", "delay") == 1, "inhibitor held before the timeout");
	WAIT_FOR(count("sleep", "delay") == 0);
	CHECK(count("sleep", "delay") == 0, "inhibitor released by the timeout");
	call_void("Emit", 1, 0);
	WAIT_FOR(woke == 2 && count("sleep", "delay") == 1);

	/* logind restarts */
	call_void("Restart", 0, 0);
	usleep(50000);
	WAIT_FOR(count("sleep", "delay") == 1 && iokitd_logind_holds_inhibitor(l));
	CHECK(count("sleep", "delay") == 1 && iokitd_logind_holds_inhibitor(l), "inhibitor taken after logind restarted");

	iokitd_logind_stop(l);
	WAIT_FOR(count("sleep", "delay") == 0);
	CHECK(count("sleep", "delay") == 0, "inhibitor released on stop");

	/* Idle assertions */
	CHECK(iokitd_idle_inhibit_acquire() == 1, "idle acquire");
	CHECK(iokitd_idle_inhibit_acquire() == 1, "second idle acquire");
	CHECK(count("idle", "block") == 1, "one idle inhibitor for two holds: %u", count("idle", "block"));
	iokitd_idle_inhibit_release();
	usleep(50000);
	CHECK(count("idle", "block") == 1, "idle inhibitor kept while held");
	iokitd_idle_inhibit_release();
	WAIT_FOR(count("idle", "block") == 0);
	CHECK(count("idle", "block") == 0, "idle inhibitor released");
	iokitd_idle_inhibit_release(); /* unbalanced release is harmless */

	/* Display assertions */
	CHECK(iokitd_screensaver_inhibit_acquire() == 1, "screensaver acquire");
	CHECK(iokitd_screensaver_inhibit_acquire() == 1, "second screensaver acquire");
	CHECK(call_u("Cookies", NULL, NULL) == 1, "one screensaver cookie");
	iokitd_screensaver_inhibit_release();
	iokitd_screensaver_inhibit_release();
	CHECK(call_u("Cookies", NULL, NULL) == 0, "screensaver uninhibited");

	/* Reset releases what is held. */
	iokitd_idle_inhibit_acquire();
	iokitd_screensaver_inhibit_acquire();
	iokitd_host_inhibit_reset();
	WAIT_FOR(count("idle", "block") == 0);
	CHECK(count("idle", "block") == 0 && call_u("Cookies", NULL, NULL) == 0, "reset released the host locks");

	/* No buses */
	setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/nonexistent/system", 1);
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/session", 1);
	time_t start = time(NULL);
	CHECK(iokitd_idle_inhibit_acquire() == 0, "idle without a bus");
	CHECK(iokitd_screensaver_inhibit_acquire() == 0, "screensaver without a bus");
	iokitd_idle_inhibit_release();
	iokitd_screensaver_inhibit_release();
	CHECK(iokitd_logind_start(&callbacks, NULL, 0) == NULL, "monitor without a bus");
	CHECK(time(NULL) - start <= 1, "failing without a bus took %ld s", (long) (time(NULL) - start));

	/* Address policy */
	check_address("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", IOKITD_SYSTEM_BUS, 1);
	check_address("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=/tmp/x", IOKITD_SYSTEM_BUS, 1);
	check_address("DBUS_SYSTEM_BUS_ADDRESS", "unixexec:path=/bin/sh,argv1=-c", IOKITD_SYSTEM_BUS, 0);
	check_address("DBUS_SYSTEM_BUS_ADDRESS", "autolaunch:", IOKITD_SYSTEM_BUS, 0);
	check_address("DBUS_SYSTEM_BUS_ADDRESS", "tcp:host=localhost,port=1", IOKITD_SYSTEM_BUS, 0);
	check_address("DBUS_SESSION_BUS_ADDRESS", "unix:path=/a;unixexec:path=/bin/sh", IOKITD_SESSION_BUS, 0);
	check_address("DBUS_SESSION_BUS_ADDRESS", "unix:path=/a b", IOKITD_SESSION_BUS, 0);
	unsetenv("DBUS_SYSTEM_BUS_ADDRESS");
	unsetenv("DBUS_SESSION_BUS_ADDRESS");
	char buffer[256];
	CHECK(iokitd_host_bus_address(IOKITD_SYSTEM_BUS, buffer, sizeof(buffer)) == 0
		&& strcmp(buffer, "unix:path=/run/dbus/system_bus_socket") == 0, "default system bus: %s", buffer);
	setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
	CHECK(iokitd_host_bus_address(IOKITD_SESSION_BUS, buffer, sizeof(buffer)) == 0
		&& strcmp(buffer, "unix:path=/run/user/1000/bus") == 0, "session bus from XDG_RUNTIME_DIR: %s", buffer);
	unsetenv("XDG_RUNTIME_DIR");
	CHECK(iokitd_host_bus_address(IOKITD_SESSION_BUS, buffer, sizeof(buffer)) != 0, "no session bus address");

	printf("%s: %d failed\n", failures ? "FAIL" : "PASS", failures);
	fflush(stdout);
	_exit(failures != 0);
}
'''

CONFIG = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:path=%s</listen>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""

procs = []
def cleanup():
    for p in reversed(procs):
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()

with tempfile.TemporaryDirectory() as tmp:
    try:
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "dbus-1"], text=True).split()
        driver = os.path.join(tmp, "driver.c")
        open(driver, "w").write(DRIVER)
        exe = os.path.join(tmp, "driver")
        subprocess.check_call(["cc", "-Wall", "-Wextra", "-Wno-unused-parameter", "-I", src, "-o", exe, driver,
                               os.path.join(src, "HostBus.c"), os.path.join(src, "LogindSleep.c"),
                               os.path.join(src, "HostPowerInhibit.c"), "-lpthread"] + flags)

        addresses = []
        for name in ("system", "session"):
            sock = os.path.join(tmp, name)
            conf = os.path.join(tmp, name + ".conf")
            open(conf, "w").write(CONFIG % sock)
            procs.append(subprocess.Popen(["dbus-daemon", "--nofork", "--config-file=" + conf]))
            addresses.append("unix:path=" + sock)
        for a in addresses:
            for _ in range(100):
                if os.path.exists(a[len("unix:path="):]):
                    break
                time.sleep(0.05)

        fake_py = os.path.join(tmp, "fake.py")
        open(fake_py, "w").write(FAKE)
        fake = subprocess.Popen([sys.executable, "-I", fake_py] + addresses, stdout=subprocess.PIPE, text=True)
        procs.append(fake)
        if fake.stdout.readline().strip() != "ready":
            sys.exit("FAIL: the fake services did not start")

        env = {k: v for k, v in os.environ.items() if not k.startswith("DBUS_") and k != "XDG_RUNTIME_DIR"}
        result = subprocess.run([exe] + addresses, env=env, timeout=90)
        sys.exit(result.returncode)
    finally:
        cleanup()
