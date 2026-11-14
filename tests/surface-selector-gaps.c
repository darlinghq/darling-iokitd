// Guest test (Darling, iokitd running): IOSurfaceRoot's connection and selectors its method table does not fill.
//
// IOSurfaceRoot's external method table is indexed by selector, and the selector enum in IOSurfacePriv.h has gaps
// (5-8, 12, 13, 16-33) and selectors iokitd does not implement (SetValue 9, GetValues 10, RemoveValue 11). A skipped
// entry has no function, and iokitd called it: the daemon died with a null program counter, and a client such as
// Activity Monitor then aborted on its dead connection. getExternalMethodForIndex also kept the first selector's
// entry in a function-local static and returned it for every later selector.
//
// XNU's IOUserClient::externalMethod answers kIOReturnUnsupported for a selector without a method. Checks, in this
// order on a fresh iokitd (the gap comes first, so the old code dies on it):
//   - a gap selector and an unimplemented one answer kIOReturnUnsupported;
//   - iokitd is alive afterwards: the next selector still reaches its own entry. Selector 1 (release, scalar in) and
//     selector 3 (unlock, struct in) both reach their stubs, KERN_NOT_SUPPORTED; with the first entry reused for
//     both, selector 3 would be dispatched with selector 1's argument shape and answer something else;
//   - the service can still be found and opened again.
// Prints PASS or the first FAIL; exit status 0 on PASS. Build as get-matching-service.c, without RAW_MIG.
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef mach_port_t io_object_t, io_service_t, io_connect_t;

#define kIOReturnUnsupported ((kern_return_t)0xe00002c7)

extern kern_return_t IOMasterPort(mach_port_t, mach_port_t*);
extern void* IOServiceMatching(const char*);
extern io_service_t IOServiceGetMatchingService(mach_port_t, void*);
extern kern_return_t IOObjectRelease(io_object_t);
extern kern_return_t IOServiceOpen(io_service_t, task_t, uint32_t, io_connect_t*);
extern kern_return_t IOServiceClose(io_connect_t);
extern kern_return_t IOConnectCallMethod(io_connect_t, uint32_t, const uint64_t*, uint32_t, const void*, size_t, uint64_t*, uint32_t*, void*, size_t*);

static void expect_kr(kern_return_t got, kern_return_t want, const char* what)
{
	if (got != want) {
		printf("FAIL: %s: got %#x, want %#x\n", what, (unsigned) got, (unsigned) want);
		exit(1);
	}
}

static io_connect_t open_surface(mach_port_t master, io_service_t* service)
{
	*service = IOServiceGetMatchingService(master, IOServiceMatching("IOSurfaceRoot"));
	if (!MACH_PORT_VALID(*service)) {
		printf("FAIL: IOSurfaceRoot not found\n");
		exit(1);
	}
	io_connect_t connect = 0;
	expect_kr(IOServiceOpen(*service, mach_task_self(), 0, &connect), KERN_SUCCESS, "IOServiceOpen(IOSurfaceRoot, 0)");
	return connect;
}

int main(void)
{
	mach_port_t master = MACH_PORT_NULL;
	if (IOMasterPort(MACH_PORT_NULL, &master) != KERN_SUCCESS || !MACH_PORT_VALID(master)) {
		printf("FAIL: IOMasterPort\n");
		return 1;
	}
	io_service_t surface;
	io_connect_t connect = open_surface(master, &surface);

	uint64_t scalar = 0;
	char unlock[8] = {0};
	expect_kr(IOConnectCallMethod(connect, 5, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL), kIOReturnUnsupported, "selector 5, a gap in the table");
	expect_kr(IOConnectCallMethod(connect, 9, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL), kIOReturnUnsupported, "selector 9, SetValue, not implemented");
	expect_kr(IOConnectCallMethod(connect, 1, &scalar, 1, NULL, 0, NULL, NULL, NULL, NULL), KERN_NOT_SUPPORTED, "selector 1, release, reaches its own stub");
	expect_kr(IOConnectCallMethod(connect, 3, NULL, 0, unlock, sizeof(unlock), NULL, NULL, NULL, NULL), KERN_NOT_SUPPORTED, "selector 3, unlock, reaches its own stub");
	expect_kr(IOConnectCallMethod(connect, 5, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL), kIOReturnUnsupported, "selector 5 again, after real selectors");

	expect_kr(IOServiceClose(connect), KERN_SUCCESS, "IOServiceClose");
	IOObjectRelease(surface);

	io_service_t again;
	io_connect_t second = open_surface(master, &again);
	expect_kr(IOServiceClose(second), KERN_SUCCESS, "IOServiceClose of the second connection");
	IOObjectRelease(again);
	printf("PASS\n");
	return 0;
}
