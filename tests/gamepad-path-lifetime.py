#!/usr/bin/env python3
# Copyright (C) 2026 Lubos Dolezel
"""Host test: IOHIDGamepadDevice::path() must not return a reference to a
temporary.

The device keeps its node path in a fixed char array, so the accessor has to
build a std::string. Returning that as a const reference hands out a dangling
reference. The test syntax-checks the real header (src/IOHIDGamepadDevice.h)
against minimal stand-ins for the Foundation, mach, dispatch and IOKit headers
with -Werror=return-stack-address, so it fails while the accessor returns a
reference and passes once it returns by value. It also checks the declared
return type.

Usage: python3 tests/gamepad-path-lifetime.py
"""
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "src")
cxx = shutil.which("clang++")
if cxx is None:
    print("SKIP: clang++ not found")
    sys.exit(0)

stubs = {
    "Foundation/NSDictionary.h": "@class NSDictionary;\n@class NSString;\n",
    "IOKit/IOKitLibPrivate.h": "",
    "IOKit/IOKitLib.h": "typedef int IOReturn;\n"
        "enum { kIOServiceRegisteredState = 1, kIOServiceMatchedState = 2 };\n",
    "mach/mach.h": "#include <stdint.h>\ntypedef unsigned mach_port_t;\n"
        "typedef int boolean_t; typedef struct { int x; } mach_msg_header_t;\n"
        "typedef int kern_return_t;\n",
    "dispatch/dispatch.h": "typedef struct dispatch_source_s *dispatch_source_t;\n"
        "typedef struct dispatch_queue_s *dispatch_queue_t;\n",
}

tmp = tempfile.mkdtemp(prefix="gamepad-path.")
try:
    for name, text in stubs.items():
        p = os.path.join(tmp, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write(text)
    unit = os.path.join(tmp, "unit.mm")
    open(unit, "w").write('#include "IOHIDGamepadDevice.h"\n')
    r = subprocess.run([cxx, "-x", "objective-c++", "-std=c++17", "-fblocks",
        "-fsyntax-only", "-Wall", "-Werror=return-stack-address",
        "-I" + src, "-I" + tmp, unit], capture_output=True, text=True)
    ok = r.returncode == 0
    if not ok:
        print(r.stderr)
    header = open(os.path.join(src, "IOHIDGamepadDevice.h")).read()
    m = re.search(r"([\w:<>& ]+?)\s+path\(\) const", header)
    byvalue = bool(m) and "&" not in m.group(1)
    print("%s: IOHIDGamepadDevice::path() compiles without a dangling reference and returns by value"
        % ("PASS" if ok and byvalue else "FAIL"))
    sys.exit(0 if ok and byvalue else 1)
finally:
    shutil.rmtree(tmp)
