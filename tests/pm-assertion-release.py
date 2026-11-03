#!/usr/bin/env python3
"""Host test for io_pm_assertion_retain_release (IOPMAssertionRetain/Release).

Cuts the assertion bookkeeping and the MIG routine out of
src/PowerAssertions.mm, builds them with $CC against minimal Mach,
dispatch, BSM and IOKit stand-ins, and checks that retain and release
of a held assertion succeed and that the last release drops the hold.
IOKitUser maps any MIG error from this routine to kIOReturnInternalError.

Usage: CC="zig c++" python3 tests/pm-assertion-release.py [source dir]
The optional source dir defaults to this checkout's src/.
"""
import os, re, shlex, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src")
NAMES = ["PowerAssertion::addRef", "PowerAssertion::delRef", "AppAssertions::AppAssertions",
         "AppAssertions::~AppAssertions", "AppAssertions::get", "AppAssertions::getAssertion",
         "AppAssertions::createAssertion", "AppAssertions::killAssertion",
         "_io_pm_assertion_retain_release"]

def cut(text, name):
    m = re.search(r"^(?:[^\s(){};]+[ \t]+)*" + re.escape(name) + r"\b[^;{]*\{", text, re.M)
    if not m:
        sys.exit("cannot find " + name)
    depth, i = 0, m.end() - 1
    while True:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[m.start():i]

source = open(os.path.join(src, "PowerAssertions.mm")).read()
parts = [cut(source, name) for name in NAMES]

HEADERS = {
    "IOKit/pwr_mgt/IOPMLib.h": """
#pragma once
typedef int kern_return_t;
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_NOT_SUPPORTED 46
enum { kIOPMAssertionMIGDoRetain = 1, kIOPMAssertionMIGDoRelease = -1 };
""",
    "dispatch/dispatch.h": """
#pragma once
#include <stdint.h>
typedef void* dispatch_source_t; typedef void* dispatch_queue_t;
#define DISPATCH_SOURCE_TYPE_PROC nullptr
#define DISPATCH_PROC_EXIT 0
static inline dispatch_queue_t dispatch_get_main_queue() { return nullptr; }
static inline dispatch_source_t dispatch_source_create(const void*, uintptr_t, unsigned long, dispatch_queue_t)
{ static int source; return &source; }
template <typename F> static inline void dispatch_source_set_event_handler(dispatch_source_t, F) {}
static inline void dispatch_resume(dispatch_source_t) {}
static inline void dispatch_release(dispatch_source_t) {}
""",
}

HARNESS = r"""
#include "PowerAssertions.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>

typedef unsigned mach_port_t; typedef int pid_t; typedef struct { unsigned val[8]; } audit_token_t;
static void audit_token_to_au32(audit_token_t t, void*, void*, void*, void*, void*, pid_t* pid, void*, void*)
{ *pid = t.val[5]; }

std::unordered_map<int, AppAssertions*> AppAssertions::m_instances;
// The process-exit handler block is built but never run here.
extern "C" { void* _NSConcreteStackBlock[32]; }

struct CountingAssertion : PowerAssertion
{
	int active = 0;
	void activate() override { active++; }
	void deactivate() override { active--; }
};

extern "C" kern_return_t _io_pm_assertion_retain_release(mach_port_t, audit_token_t, int, int, int*, int*, int*, int*);

@@PARTS@@

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main()
{
	CountingAssertion display;
	audit_token_t token = {{0, 0, 0, 0, 0, 4242, 0, 0}};
	int id = AppAssertions::get(4242)->createAssertion(&display);

	int count = -1, disable = 0, enable = 0, rc = -1;
	kern_return_t kr = _io_pm_assertion_retain_release(0, token, id, kIOPMAssertionMIGDoRetain, &count, &disable, &enable, &rc);
	CHECK(kr == KERN_SUCCESS && rc == 0 && count == 2, "retain: kr=%d rc=%d count=%d, want 0 0 2", kr, rc, count);

	rc = -1;
	kr = _io_pm_assertion_retain_release(0, token, id, kIOPMAssertionMIGDoRelease, &count, &disable, &enable, &rc);
	CHECK(kr == KERN_SUCCESS && rc == 0 && count == 1, "first release: kr=%d rc=%d count=%d, want 0 0 1", kr, rc, count);
	CHECK(display.active == 1, "hold dropped while still retained");

	rc = -1;
	kr = _io_pm_assertion_retain_release(0, token, id, kIOPMAssertionMIGDoRelease, &count, &disable, &enable, &rc);
	CHECK(kr == KERN_SUCCESS && rc == 0 && count == 0, "last release: kr=%d rc=%d count=%d, want 0 0 0", kr, rc, count);
	CHECK(display.active == 0, "last release kept the hold");
	CHECK(AppAssertions::get(4242)->getAssertion(id) == nullptr, "released assertion still listed");

	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    for rel, text in HEADERS.items():
        os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
        open(os.path.join(tmp, rel), "w").write(text)
    open(os.path.join(tmp, "PowerAssertions.h"), "w").write(open(os.path.join(src, "PowerAssertions.h")).read())
    test = os.path.join(tmp, "test.cpp")
    open(test, "w").write(HARNESS.replace("@@PARTS@@", "\n\n".join(parts)))
    exe = os.path.join(tmp, "test")
    cc = shlex.split(os.environ.get("CC", "c++"))
    subprocess.run(cc + ["-x", "c++", "-std=c++17", "-fblocks", "-I", tmp, "-o", exe, test], check=True)
    sys.exit(subprocess.run([exe]).returncode)
