#!/usr/bin/env python3
"""Host test for iokitd's display identification (HostDisplay.c).

HostDisplay.c is built natively together with tests/host-display-test.c. It
checks the EDID parser (identifiers, dates, name and serial descriptors,
physical size from the preferred timing and from the centimetre fields, and
the rejection of short, mis-checksummed and header-less blocks) on blocks
that the test synthesises, and the discovery of DRM connectors in a fake
sysfs class directory (connected and enabled only, empty EDID, ordering and
the limit).

Usage: python3 tests/host-display.py
"""
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
if shutil.which("cc") is None:
    print("SKIP: cc is not installed")
    sys.exit(0)

work = tempfile.mkdtemp(prefix="display-test.")
try:
    exe = os.path.join(work, "test")
    subprocess.check_call(["cc", "-Wall", "-Wextra", "-o", exe,
                           os.path.join(here, "host-display-test.c"),
                           os.path.join(here, "..", "src", "HostDisplay.c")])
    sysfs = os.path.join(work, "drm")
    os.mkdir(sysfs)
    sys.exit(subprocess.call([exe, sysfs]))
finally:
    shutil.rmtree(work, ignore_errors=True)
