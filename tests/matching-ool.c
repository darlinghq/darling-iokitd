// Copyright (C) 2026 Lubos Dolezel
//
// Guest test (Darling, iokitd running; also runs on macOS): matching requests that IOKit sends out of line,
// the notification variants, the IORegistryEntry class chain and the connection to IOSurfaceRoot.
//
// IOKit sends a matching dictionary out of line when its serialized form does not fit the in-line request
// (4096 bytes). Two large dictionaries are used: one whose only key is an IOProviderClass that is not a string,
// which matching skips, so it matches every service; and one with a large IOPropertyMatch that no service
// has, so it matches none. Checks:
//   - io_server_version answers the version that has the binary forms (20140421), where IOKit exports it;
//   - IOServiceGetMatchingService, IOServiceGetMatchingServices and IOServiceAddMatchingNotification with the
//     large dictionaries find services and none respectively;
//   - IOServiceMatching("IORegistryEntry") matches services, as every service is a registry entry;
//   - IOSurfaceRoot opens for connect type 0.
// Prints PASS or the first FAIL; exit status 0 on PASS.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>

#include <dlfcn.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static CFMutableDictionaryRef largeMatching(int matchesAll)
{
	CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFMutableArrayRef padding = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	for (int i = 0; i < 200; i++) {
		CFStringRef s = CFStringCreateWithFormat(NULL, NULL, CFSTR("padding entry number %d to make the request large"), i);
		CFArrayAppendValue(padding, s);
		CFRelease(s);
	}
	if (matchesAll) {
		CFDictionarySetValue(d, CFSTR("IOProviderClass"), padding);
	} else {
		CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFDictionarySetValue(props, CFSTR("NoServiceHasThisPropertyAnywhere"), padding);
		CFDictionarySetValue(d, CFSTR("IOPropertyMatch"), props);
		CFRelease(props);
	}
	CFRelease(padding);
	CFDataRef bytes = CFPropertyListCreateData(NULL, d, kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	CHECK(bytes && CFDataGetLength(bytes) > 4096, "the test dictionary is not large");
	if (bytes) CFRelease(bytes);
	return d;
}

static unsigned countIterator(io_iterator_t it)
{
	unsigned n = 0;
	io_object_t o;
	while ((o = IOIteratorNext(it)) != IO_OBJECT_NULL) { n++; IOObjectRelease(o); }
	return n;
}

static void gotMatch(void* refcon, io_iterator_t iterator) { }

int main(void)
{
	mach_port_t master = MACH_PORT_NULL;
	if (IOMasterPort(MACH_PORT_NULL, &master) != KERN_SUCCESS) { printf("FAIL: IOMasterPort\n"); return 1; }

	// The MIG routine is exported by IOKit on some architectures only: the check runs where it is.
	kern_return_t kr;
	kern_return_t (*serverVersion)(mach_port_t, uint64_t*) = dlsym(RTLD_DEFAULT, "io_server_version");
	if (serverVersion) {
		uint64_t version = 0;
		kr = serverVersion(master, &version);
		CHECK(kr == KERN_SUCCESS && version >= 20140421, "io_server_version: %#x, %llu", kr, (unsigned long long) version);
	}

	// One service, large request
	io_service_t any = IOServiceGetMatchingService(master, (CFDictionaryRef) largeMatching(1));
	CHECK(any != IO_OBJECT_NULL, "large IOServiceGetMatchingService found no service");
	if (any != IO_OBJECT_NULL) IOObjectRelease(any);
	io_service_t none = IOServiceGetMatchingService(master, (CFDictionaryRef) largeMatching(0));
	CHECK(none == IO_OBJECT_NULL, "large IOServiceGetMatchingService found a service for a property nothing has");
	if (none != IO_OBJECT_NULL) IOObjectRelease(none);

	// Several services, large request
	io_iterator_t it = IO_OBJECT_NULL;
	kr = IOServiceGetMatchingServices(master, (CFDictionaryRef) largeMatching(1), &it);
	CHECK(kr == KERN_SUCCESS && it != IO_OBJECT_NULL, "large IOServiceGetMatchingServices: %#x", kr);
	if (it != IO_OBJECT_NULL) { CHECK(countIterator(it) >= 1, "large IOServiceGetMatchingServices found no service"); IOObjectRelease(it); }
	it = IO_OBJECT_NULL;
	kr = IOServiceGetMatchingServices(master, (CFDictionaryRef) largeMatching(0), &it);
	// Where nothing matches, macOS returns no iterator at all and iokitd an empty one.
	CHECK(kr == KERN_SUCCESS && (it == IO_OBJECT_NULL || countIterator(it) == 0), "large IOServiceGetMatchingServices for a property nothing has: %#x", kr);
	if (it != IO_OBJECT_NULL) IOObjectRelease(it);

	// Notification, large request: the matching service is waiting in the iterator, an unknown class has none
	IONotificationPortRef port = IONotificationPortCreate(master);
	CHECK(port != NULL, "IONotificationPortCreate");
	it = IO_OBJECT_NULL;
	kr = IOServiceAddMatchingNotification(port, kIOFirstMatchNotification, (CFDictionaryRef) largeMatching(1), gotMatch, NULL, &it);
	CHECK(kr == KERN_SUCCESS && it != IO_OBJECT_NULL, "large IOServiceAddMatchingNotification: %#x", kr);
	if (it != IO_OBJECT_NULL) { CHECK(countIterator(it) >= 1, "large notification has no existing match"); IOObjectRelease(it); }
	it = IO_OBJECT_NULL;
	kr = IOServiceAddMatchingNotification(port, kIOFirstMatchNotification, (CFDictionaryRef) largeMatching(0), gotMatch, NULL, &it);
	CHECK(kr == KERN_SUCCESS && it != IO_OBJECT_NULL && countIterator(it) == 0, "large notification for a property nothing has: %#x", kr);
	if (it != IO_OBJECT_NULL) IOObjectRelease(it);
	IONotificationPortDestroy(port);

	// Class chain: every service is an IORegistryEntry
	it = IO_OBJECT_NULL;
	kr = IOServiceGetMatchingServices(master, IOServiceMatching("IORegistryEntry"), &it);
	CHECK(kr == KERN_SUCCESS && it != IO_OBJECT_NULL, "IOServiceMatching(IORegistryEntry): %#x", kr);
	if (it != IO_OBJECT_NULL) { CHECK(countIterator(it) >= 1, "IORegistryEntry matched no service"); IOObjectRelease(it); }

	// IOSurfaceRoot connection
	io_service_t surface = IOServiceGetMatchingService(master, IOServiceMatching("IOSurfaceRoot"));
	CHECK(surface != IO_OBJECT_NULL, "IOSurfaceRoot not found");
	if (surface != IO_OBJECT_NULL) {
		io_connect_t connect = IO_OBJECT_NULL;
		kr = IOServiceOpen(surface, mach_task_self(), 0, &connect);
		CHECK(kr == KERN_SUCCESS && connect != IO_OBJECT_NULL, "IOServiceOpen(IOSurfaceRoot, 0): %#x", kr);
		if (connect != IO_OBJECT_NULL) IOServiceClose(connect);
		IOObjectRelease(surface);
	}

	printf(failures ? "FAILED (%d)\n" : "PASS\n", failures);
	return failures ? 1 : 0;
}
