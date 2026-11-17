# iokitd: out-of-line matching requests, class chain, IOSurfaceRoot connection

Build matching-ool.c against IOKit and CoreFoundation, run it in the guest (x86_64 and i386) and on macOS.

Installed baseline: the large-request checks fail (the out-of-line requests were unsupported), IOServiceMatching("IORegistryEntry") finds nothing, and IOSurfaceRoot cannot be opened. Candidate: PASS. The io_server_version check runs only where IOKit exports that routine (the 32-bit slice in Darling).
