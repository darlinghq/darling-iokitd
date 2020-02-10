#include "iokitd.h"
#include "Registry.h"
#include <IOCFUnserialize.h>
#include <CoreFoundation/CFString.h>
#include <os/log.h>
#include <stdexcept>

extern "C" {
#include "iokitmigServer.h"
}

void Registry::registerService(IOService* service)
{
	// TODO
}

kern_return_t is_io_service_get_matching_services_ool
(
	mach_port_t master_port,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	kern_return_t *result,
	mach_port_t *existing
)
{
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_matching_services_bin
(
	mach_port_t master_port,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t *existing
)
{
	CFStringRef errorString = nullptr;
	try
	{
		CFTypeRef criteria = IOCFUnserializeBinary(matching, matchingCnt, nullptr, 0, &errorString);

		if (!criteria)
			throwCFStringException(CFSTR("io_service_get_matching_services_bin(): cannot parse 'matching': %@"), errorString);
		
		if (CFGetTypeID(criteria) != CFDictionaryGetTypeID())
			throw std::runtime_error("io_service_get_matching_services_bin(): dictionary expected");

		NSDictionary* dictCriteria = (NSDictionary*) criteria;

		// dictCriteria example:
		// IOProviderClass -> IODisplayConnect

		CFShow(criteria);
		CFRelease(criteria);

		return KERN_NOT_SUPPORTED;
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, e.what());
		if (errorString)
			CFRelease(errorString);

		return KERN_INVALID_ARGUMENT;
	}
}
