#!/usr/bin/env python3
"""Host test for io_registry_entry_get_property_bytes (IORegistryEntryGetProperty).

Cuts the routine out of src/IORegistryEntry.mm (or, while it is still a
stub, out of src/stubs.c), builds it with $CC against minimal Mach, IOKit
and CoreFoundation stand-ins, and checks the bytes returned for data,
string, boolean and integer properties and the error cases.

Usage: CC="zig c++" python3 tests/registry-property-bytes.py [source dir]
The optional source dir defaults to this checkout's src/.
"""
import os, re, shlex, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src")
NAME = "is_io_registry_entry_get_property_bytes"

def cut(text):
    m = re.search(r"^kern_return_t\s+" + NAME + r"\b[^;{]*\{", text, re.M)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while True:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[m.start():i]

routine = cut(open(os.path.join(src, "IORegistryEntry.mm")).read()) or cut(open(os.path.join(src, "stubs.c")).read())
if routine is None:
    sys.exit("cannot find " + NAME)

HEADERS = {
    "mach/mach.h": """
#pragma once
#include <stdint.h>
typedef unsigned mach_port_t; typedef int kern_return_t; typedef int boolean_t;
typedef unsigned mach_msg_type_number_t; typedef struct { int unused; } mach_msg_header_t;
#define KERN_NOT_SUPPORTED 46
""",
    "dispatch/dispatch.h": "#pragma once\ntypedef void* dispatch_source_t;\n",
    "Foundation/NSDictionary.h": "#pragma once\nstruct NSDictionary;\n",
}

# CoreFoundation stand-in: just enough object model for the routine.
HARNESS = r"""
#include "IORegistryEntry.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

typedef char io_name_t[128]; typedef char io_struct_inband_t[4096]; typedef int IOReturn;
#define kIOReturnSuccess 0
#define kIOReturnNoResources ((IOReturn)0xe00002be)
#define kIOReturnIPCError ((IOReturn)0xe00002bf)
#define kIOReturnBadArgument ((IOReturn)0xe00002c2)
#define STUB()

typedef long CFIndex; typedef unsigned long CFTypeID; typedef unsigned char UInt8;
enum { kStr = 1, kData, kBool, kNum, kDict };
struct CFObj { CFTypeID type; std::string bytes; bool yes; int64_t num; CFIndex size; bool isFloat;
               std::map<std::string, CFObj*> dict; };
typedef const CFObj *CFTypeRef, *CFStringRef, *CFDataRef, *CFBooleanRef, *CFNumberRef, *CFDictionaryRef;
#define kCFAllocatorDefault nullptr
#define kCFStringEncodingUTF8 0
#define kCFNumberSInt64Type 4
static int g_live;
static CFStringRef CFStringCreateWithCString(void*, const char* s, int) { g_live++; return new CFObj{kStr, s}; }
static void CFRelease(CFTypeRef o) { g_live--; delete o; }
static CFTypeID CFGetTypeID(CFTypeRef o) { return o->type; }
static CFTypeID CFStringGetTypeID() { return kStr; }
static CFTypeID CFDataGetTypeID() { return kData; }
static CFTypeID CFBooleanGetTypeID() { return kBool; }
static CFTypeID CFNumberGetTypeID() { return kNum; }
static CFTypeRef CFDictionaryGetValue(CFDictionaryRef d, const void* k)
{ auto it = d->dict.find(((CFStringRef) k)->bytes); return it == d->dict.end() ? nullptr : it->second; }
static const UInt8* CFDataGetBytePtr(CFDataRef d) { return (const UInt8*) d->bytes.data(); }
static CFIndex CFDataGetLength(CFDataRef d) { return d->bytes.size(); }
static bool CFStringGetCString(CFStringRef s, char* buf, CFIndex size, int)
{ if ((CFIndex) s->bytes.size() + 1 > size) return false; memcpy(buf, s->bytes.c_str(), s->bytes.size() + 1); return true; }
static bool CFBooleanGetValue(CFBooleanRef b) { return b->yes; }
static bool CFNumberIsFloatType(CFNumberRef n) { return n->isFloat; }
static CFIndex CFNumberGetByteSize(CFNumberRef n) { return n->size; }
static bool CFNumberGetValue(CFNumberRef n, int, void* out) { memcpy(out, &n->num, 8); return true; }

std::unordered_map<mach_port_t, IOObject*> IOObject::m_objects;
static mach_port_t g_nextPort = 100;
IOObject::IOObject() : m_port(g_nextPort++) { m_objects[m_port] = this; }
IOObject::~IOObject() { m_objects.erase(m_port); }
bool IOObject::conformsTo(const char*) { return false; }
IOObject* IOObject::lookup(mach_port_t p) { auto it = m_objects.find(p); return it == m_objects.end() ? nullptr : it->second; }
IORegistryEntry::IORegistryEntry() {}
IORegistryEntry::~IORegistryEntry() {}
const char* IORegistryEntry::className() const { return "IORegistryEntry"; }
static CFObj g_props{kDict};
NSDictionary* IORegistryEntry::getProperties() { return (NSDictionary*) &g_props; }

extern "C" kern_return_t is_io_registry_entry_get_property_bytes(mach_port_t, io_name_t, io_struct_inband_t,
    mach_msg_type_number_t*);

@@ROUTINE@@

static int failures, checks;
static void expect(IORegistryEntry& e, const char* name, mach_msg_type_number_t capacity,
                   kern_return_t wantKr, const void* want, mach_msg_type_number_t wantCnt)
{
	static io_name_t key; strncpy(key, name, sizeof(key));
	static io_struct_inband_t buf; memset(buf, 0x55, sizeof(buf));
	mach_msg_type_number_t cnt = capacity;
	kern_return_t kr = is_io_registry_entry_get_property_bytes(e.port(), key, buf, &cnt);
	checks++;
	if (kr != wantKr || (wantKr == kIOReturnSuccess && (cnt != wantCnt || memcmp(buf, want, cnt)))) {
		failures++;
		printf("FAIL: %s: kr=%#x cnt=%u, want kr=%#x cnt=%u\n", name, kr, cnt, wantKr, wantCnt);
	}
}

int main()
{
	IORegistryEntry e;
	g_props.dict["IOPlatformSerialNumber"] = new CFObj{kStr, "C02SG0000000"};
	g_props.dict["clock-frequency"] = new CFObj{kData, std::string("\x00\xe1\xf5\x05", 4)};
	g_props.dict["built-in"] = new CFObj{kBool, "", true};
	g_props.dict["removable"] = new CFObj{kBool, "", false};
	g_props.dict["unit"] = new CFObj{kNum, "", false, 0x1234, 2};
	g_props.dict["id"] = new CFObj{kNum, "", false, 0x0102030405060708, 8};
	g_props.dict["ratio"] = new CFObj{kNum, "", false, 0, 8, true};
	g_props.dict["nested"] = new CFObj{kDict};

	const unsigned char unit[] = {0x34, 0x12}, id[] = {8, 7, 6, 5, 4, 3, 2, 1};
	expect(e, "IOPlatformSerialNumber", 4096, kIOReturnSuccess, "C02SG0000000", 13);
	expect(e, "clock-frequency", 4096, kIOReturnSuccess, "\x00\xe1\xf5\x05", 4);
	expect(e, "built-in", 4096, kIOReturnSuccess, "Yes", 4);
	expect(e, "removable", 4096, kIOReturnSuccess, "No", 3);
	expect(e, "unit", 4096, kIOReturnSuccess, unit, 2);
	expect(e, "id", 4096, kIOReturnSuccess, id, 8);
	expect(e, "IOPlatformSerialNumber", 12, kIOReturnIPCError, nullptr, 0);
	expect(e, "absent", 4096, kIOReturnNoResources, nullptr, 0);
	expect(e, "ratio", 4096, kIOReturnBadArgument, nullptr, 0);
	expect(e, "nested", 4096, kIOReturnBadArgument, nullptr, 0);
	checks++;
	if (g_live) { failures++; printf("FAIL: %d CFString keys leaked\n", g_live); }

	printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
	return failures != 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    for rel, text in HEADERS.items():
        os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
        open(os.path.join(tmp, rel), "w").write(text)
    for h in ("IOObject.h", "IORegistryEntry.h"):
        open(os.path.join(tmp, h), "w").write(open(os.path.join(src, h)).read())
    test = os.path.join(tmp, "test.cpp")
    open(test, "w").write(HARNESS.replace("@@ROUTINE@@", routine))
    exe = os.path.join(tmp, "test")
    cc = shlex.split(os.environ.get("CC", "c++"))
    subprocess.run(cc + ["-x", "c++", "-std=c++17", "-I", tmp, "-o", exe, test], check=True)
    sys.exit(subprocess.run([exe]).returncode)
