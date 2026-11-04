#!/usr/bin/env python3
"""Host test for the registry path and plane MIG routines in src/IORegistryEntry.mm.

Cuts the registry functions out of IORegistryEntry.mm (or, where a routine
is still a stub, out of stubs.c), builds them against minimal Mach, IOKit
and Foundation stand-ins with $CC, and checks what IOKitUser's
IORegistryEntryCopyFromPath, IORegistryEntryCopyPath and
IORegistryEntryInPlane receive.

Usage: CC="zig c++" python3 tests/registry-path-plane.py [source dir]
The optional source dir defaults to this checkout's src/.
"""
import os, re, shlex, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src")

ROUTINES = ["is_io_registry_entry_from_path", "is_io_registry_entry_from_path_ool",
            "is_io_registry_entry_get_path_ool", "is_io_registry_entry_in_plane"]
METHODS = ["IORegistryEntry::IORegistryEntry", "IORegistryEntry::~IORegistryEntry",
           "IORegistryEntry::getParents", "IORegistryEntry::getChildren",
           "IORegistryEntry::registerInPlane", "IORegistryEntry::getPath",
           "IORegistryEntry::root", "IORegistryEntry::getName"]
OPTIONAL = ["lookupPath"]

def cut(text, name):
    """Return (offset, definition) of the function or class `name`, or None."""
    m = re.search(r"^(class\s+|(?:[^\s(){};]+[ \t]+)+)?" + re.escape(name) + r"\b[^;{]*\{", text, re.M)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while True:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            break
    return m.start(), text[m.start():i] + (";" if m.group(1) and m.group(1).startswith("class") else "")

registry = open(os.path.join(src, "IORegistryEntry.mm")).read()
stubs = open(os.path.join(src, "stubs.c")).read()

found, stubbed = [], []
for name in METHODS + ["RootIORegistryEntry"] + OPTIONAL + ROUTINES:
    hit = cut(registry, name)
    if hit:
        found.append(hit)
    elif name in ROUTINES and cut(stubs, name):
        stubbed.append(cut(stubs, name))
    elif name not in OPTIONAL:
        sys.exit("cannot find %s" % name)
# Keep the file order so helpers precede their callers.
parts = [body for _, body in sorted(found) + stubbed]

HEADERS = {
    "mach/mach.h": """
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned mach_port_t; typedef int kern_return_t; typedef int boolean_t;
typedef unsigned mach_msg_type_number_t; typedef uintptr_t vm_address_t; typedef uintptr_t vm_size_t;
typedef struct { int unused; } mach_msg_header_t;
#define KERN_SUCCESS 0
#define KERN_NOT_SUPPORTED 46
#define MACH_PORT_NULL 0u
#define TRUE 1
#define FALSE 0
static inline mach_port_t mach_task_self(void) { return 1; }
extern vm_address_t g_deallocated; extern vm_size_t g_deallocatedSize;
static inline kern_return_t vm_deallocate(mach_port_t, vm_address_t a, vm_size_t s)
{ g_deallocated = a; g_deallocatedSize = s; return KERN_SUCCESS; }
""",
    "dispatch/dispatch.h": "#pragma once\ntypedef void* dispatch_source_t;\n",
    "Foundation/NSDictionary.h": "#pragma once\nstruct NSDictionary;\n",
    "iokit_stub.h": """
#pragma once
#include <mach/mach.h>
#include <strings.h>
typedef char io_name_t[128]; typedef char io_string_t[512]; typedef char io_string_inband_t[4096];
typedef char* io_buf_ptr_t; typedef kern_return_t IOReturn;
#define kIOReturnSuccess 0
#define kIOReturnNoDevice ((IOReturn)0xe00002c0)
#define kIOReturnBadArgument ((IOReturn)0xe00002c2)
#define kIOReturnNoSpace ((IOReturn)0xe00002db)
#define kIOServicePlane "IOService"
#define STUB()
""",
}

HARNESS = r"""
#include "iokit_stub.h"
#include "IORegistryEntry.h"
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <unordered_map>
#include <sstream>
#include <stack>
#include <vector>

vm_address_t g_deallocated; vm_size_t g_deallocatedSize;
std::unordered_map<mach_port_t, IOObject*> IOObject::m_objects;
static mach_port_t g_nextPort = 100;
IOObject::IOObject() : m_port(g_nextPort++) { m_objects[m_port] = this; }
IOObject::~IOObject() { m_objects.erase(m_port); }
bool IOObject::conformsTo(const char*) { return false; }
void IOObject::retain() {}
void IOObject::release() {}
IOObject* IOObject::lookup(mach_port_t p) { auto it = m_objects.find(p); return it == m_objects.end() ? nullptr : it->second; }
uint64_t IORegistryEntry::m_nextId = 0x100000000;
std::unordered_map<uint64_t, IORegistryEntry*> IORegistryEntry::m_entriesById;
std::shared_mutex IORegistryEntry::m_entriesByIdMutex;
NSDictionary* IORegistryEntry::getProperties() { return nullptr; }
const char* IORegistryEntry::className() const { return "IORegistryEntry"; }

extern "C" {
kern_return_t is_io_registry_entry_from_path(mach_port_t, io_string_t, mach_port_t*);
kern_return_t is_io_registry_entry_from_path_ool(mach_port_t, io_string_inband_t, io_buf_ptr_t,
    mach_msg_type_number_t, kern_return_t*, mach_port_t*);
kern_return_t is_io_registry_entry_get_path_ool(mach_port_t, io_name_t, io_string_inband_t,
    io_buf_ptr_t*, mach_msg_type_number_t*);
kern_return_t is_io_registry_entry_in_plane(mach_port_t, io_name_t, boolean_t*);
}

@@PARTS@@

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main()
{
	IORegistryEntry display, output, platform;
	display.registerInPlane(kIOServicePlane, "X11Display", IORegistryEntry::root());
	output.registerInPlane(kIOServicePlane, "DP-1", &display);
	platform.registerInPlane(kIOServicePlane, "IOPlatformExpertDevice", IORegistryEntry::root());
	static io_name_t service = kIOServicePlane, deviceTree = "IODeviceTree";

	// IORegistryEntryInPlane
	boolean_t in = FALSE;
	kern_return_t kr = is_io_registry_entry_in_plane(output.port(), service, &in);
	CHECK(kr == KERN_SUCCESS && in, "in_plane(DP-1, IOService): kr=%#x in=%d, want 0 and 1", kr, in);
	in = TRUE;
	kr = is_io_registry_entry_in_plane(output.port(), deviceTree, &in);
	CHECK(kr == KERN_SUCCESS && !in, "in_plane(DP-1, IODeviceTree): kr=%#x in=%d, want 0 and 0", kr, in);

	// IORegistryEntryCopyFromPath, short path sent inband
	static io_string_inband_t inband = "IOService:/X11Display/DP-1";
	kern_return_t result = -1; mach_port_t entry = 0;
	kr = is_io_registry_entry_from_path_ool(0, inband, nullptr, 0, &result, &entry);
	CHECK(kr == KERN_SUCCESS && result == kIOReturnSuccess && entry == output.port(),
	      "from_path_ool inband: kr=%#x result=%#x entry=%u, want 0 0 %u", kr, result, entry, output.port());

	// The same path sent out of line, inband path empty
	static io_string_inband_t empty = "";
	char* ool = strdup("IOService:/IOPlatformExpertDevice");
	g_deallocated = 0;
	kr = is_io_registry_entry_from_path_ool(0, empty, ool, strlen(ool) + 1, &result, &entry);
	CHECK(kr == KERN_SUCCESS && result == kIOReturnSuccess && entry == platform.port(),
	      "from_path_ool out of line: kr=%#x result=%#x entry=%u, want 0 0 %u", kr, result, entry, platform.port());
	CHECK(g_deallocated == (vm_address_t) ool && g_deallocatedSize == strlen(ool) + 1,
	      "from_path_ool did not release the out-of-line path");

	// Out-of-line path without its NUL
	entry = 1234;
	kr = is_io_registry_entry_from_path_ool(0, empty, ool, strlen(ool), &result, &entry);
	CHECK(kr == KERN_SUCCESS && result == kIOReturnBadArgument && entry == MACH_PORT_NULL,
	      "from_path_ool unterminated: kr=%#x result=%#x entry=%u, want 0 %#x 0", kr, result, entry, kIOReturnBadArgument);

	// Missing entry
	static io_string_inband_t missing = "IOService:/X11Display/HDMI-9";
	kr = is_io_registry_entry_from_path_ool(0, missing, nullptr, 0, &result, &entry);
	CHECK(kr == KERN_SUCCESS && result != kIOReturnSuccess && entry == MACH_PORT_NULL,
	      "from_path_ool missing: kr=%#x result=%#x entry=%u, want 0, an error, 0", kr, result, entry);

	// io_registry_entry_from_path keeps working through the shared lookup
	static io_string_t shortPath = "IOService:/X11Display";
	kr = is_io_registry_entry_from_path(0, shortPath, &entry);
	CHECK(kr == KERN_SUCCESS && entry == display.port(), "from_path: kr=%#x entry=%u", kr, entry);

	// IORegistryEntryCopyPath
	static io_string_inband_t path; io_buf_ptr_t pathOol = (io_buf_ptr_t) 1; mach_msg_type_number_t pathOolCnt = 99;
	kr = is_io_registry_entry_get_path_ool(output.port(), service, path, &pathOol, &pathOolCnt);
	CHECK(kr == KERN_SUCCESS && !strcmp(path, "IOService:/X11Display/DP-1") && !pathOol && !pathOolCnt,
	      "get_path_ool: kr=%#x path='%s' ool=%p cnt=%u", kr, kr ? "" : path, (void*) pathOol, pathOolCnt);

	free(ool);
	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    for rel, text in HEADERS.items():
        os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
        open(os.path.join(tmp, rel), "w").write(text)
    # Only IOObject.h and IORegistryEntry.h come from the tree; the cut code
    # supplies its own includes through the harness.
    for h in ("IOObject.h", "IORegistryEntry.h"):
        open(os.path.join(tmp, h), "w").write(open(os.path.join(src, h)).read())
    test = os.path.join(tmp, "test.cpp")
    open(test, "w").write(HARNESS.replace("@@PARTS@@", "\n\n".join(parts)))
    exe = os.path.join(tmp, "test")
    cc = shlex.split(os.environ.get("CC", "c++"))
    subprocess.run(cc + ["-x", "c++", "-std=c++17", "-I", tmp, "-o", exe, test], check=True)
    sys.exit(subprocess.run([exe]).returncode)
