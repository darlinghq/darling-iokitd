#include "IOService.h"

extern "C" {
#include "iokitmigServer.h"
}

IOService::IOService()
{

}

// https://unix.superglobalmegacorp.com/xnu/newsrc/iokit/KernelConfigTables.cpp.html
bool IOService::matches(NSDictionary* dict)
{
	NSDictionary* ourProps = matchingDictionary();

	for (NSString* key in dict)
	{
		if (![dict[key] isEqual: ourProps[key]])
			return false;
	}
	return true;
}

bool IOService::conformsTo(const char* className)
{
	if (strcmp(className, "IOService") == 0)
		return true;
	return IOObject::conformsTo(className);
}

kern_return_t is_io_service_get_state
(
	mach_port_t service,
	uint64_t *state,
	uint32_t *busy_state,
	uint64_t *accumulated_busy_time
)
{
    IOService* e = dynamic_cast<IOService*>(IOObject::lookup(service));
	if (!e)
		return kIOReturnBadArgument;

	*state = e->state();
	*busy_state = e->busyState();
	*accumulated_busy_time = e->accumulatedBusyTime();

    return kIOReturnSuccess;
}
