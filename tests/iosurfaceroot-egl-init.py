#!/usr/bin/env python3
"""Host test for the IOSurfaceRoot constructor in src/IOSurfaceRoot.mm.

Cuts the constructor out of the source, builds it against a scripted EGL
stand-in and checks that a failing eglInitialize or eglChooseConfig (or a
config count of zero) leaves hasDisplay() false and is reported on stderr,
and that a working EGL still gives a display and binds the API.

Usage: CXX=clang++ python3 tests/iosurfaceroot-egl-init.py [source dir]
The optional source dir defaults to this checkout's src/.
"""
import os, re, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src")
text = open(os.path.join(src, "IOSurfaceRoot.mm")).read()

m = re.search(r"^IOSurfaceRoot::IOSurfaceRoot\(\)\n\{", text, re.M)
if not m:
    sys.exit("IOSurfaceRoot constructor not found")
depth, i = 0, m.end() - 1
while True:
    depth += {"{": 1, "}": -1}.get(text[i], 0)
    i += 1
    if depth == 0:
        break
ctor = text[m.start():i]

program = r'''
#include <iostream>
#include <sstream>
#include <cstdlib>
typedef void* EGLDisplay; typedef void* EGLConfig; typedef int EGLint; typedef unsigned EGLBoolean;
typedef unsigned EGLenum;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_OPENGL_API 0x30A2
#define EGL_PLATFORM_X11_KHR 0x31D5
static int init_ok, choose_ok, choose_count, terminated, bound;
static int dummy;
EGLDisplay eglGetPlatformDisplay(EGLenum, void*, const EGLint*) { return (EGLDisplay)&dummy; }
EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*) { return init_ok; }
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint* n) { *n = choose_count; return choose_ok; }
EGLBoolean eglTerminate(EGLDisplay) { terminated++; return 1; }
EGLBoolean eglBindAPI(EGLenum) { bound++; return 1; }
EGLint eglGetError() { return 0x3001; }
struct IODisplayConnectX11 { static void* getDisplay() { return 0; } };
struct IOSurfaceRoot {
	EGLDisplay m_display;
	bool hasDisplay() const { return m_display != EGL_NO_DISPLAY; }
	IOSurfaceRoot();
};
CTOR
static int run(int i, int c, int n, bool want_display, bool want_msg, int want_term, int want_bind) {
	init_ok = i; choose_ok = c; choose_count = n; terminated = bound = 0;
	std::ostringstream err; auto* old = std::cerr.rdbuf(err.rdbuf());
	IOSurfaceRoot r;
	std::cerr.rdbuf(old);
	bool msg = err.str().find("IOSurfaceRoot:") != std::string::npos;
	bool ok = r.hasDisplay() == want_display && msg == want_msg && terminated == want_term && bound == want_bind;
	if (!ok) std::cout << "FAIL init=" << i << " choose=" << c << " n=" << n << ": display=" << r.hasDisplay()
		<< " msg=" << msg << " terminated=" << terminated << " bound=" << bound << std::endl;
	return ok ? 0 : 1;
}
int main() {
	int bad = 0;
	bad += run(1, 1, 1, true,  false, 0, 1);
	bad += run(0, 1, 1, false, true,  0, 0);
	bad += run(1, 0, 0, false, true,  1, 0);
	bad += run(1, 1, 0, false, true,  1, 0);
	if (!bad) std::cout << "PASS: EGL init and config failures leave no display and are reported" << std::endl;
	return bad;
}
'''.replace("CTOR", ctor)

with tempfile.TemporaryDirectory() as d:
    cpp, exe = os.path.join(d, "t.cpp"), os.path.join(d, "t")
    open(cpp, "w").write(program)
    cxx = os.environ.get("CXX", "clang++")
    subprocess.run([cxx, "-std=c++17", "-Wall", "-fsanitize=address,undefined", cpp, "-o", exe], check=True)
    sys.exit(subprocess.run([exe]).returncode)
