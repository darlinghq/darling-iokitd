#include "iokitd.h"
#include "ServiceRegistry.h"
#include <IOCFUnserialize.h>
#include <CoreFoundation/CFString.h>
#include <IOKit/IOReturn.h>
#include <os/log.h>
#include <stdexcept>
#include "IOIterator.h"

extern "C" {
#include "iokitmigServer.h"
}

ServiceRegistry* ServiceRegistry::instance()
{
	static ServiceRegistry reg;
	return &reg;
}

IOIterator* ServiceRegistry::iteratorForMatchingServices(NSDictionary* criteria) const
{
	std::vector<IOObject*> matching;

	for (auto it = m_registeredServices.begin(); it != m_registeredServices.end(); it++)
	{
		if ((*it)->matches(criteria))
			matching.push_back(*it);
	}

	return new IOIterator(matching);
}

void ServiceRegistry::registerService(IOService* service)
{
	m_registeredServices.push_back(service);
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
	// TODO
    return kIOReturnUnsupported;
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

		// Criteria example:
		// IOProviderClass -> IODisplayConnect
		IOIterator* iterator = ServiceRegistry::instance()->iteratorForMatchingServices((NSDictionary*) criteria);
		CFShow(criteria);
		CFRelease(criteria);

		*existing = iterator->port();

		iterator->releaseLater();

		return kIOReturnSuccess;
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, "is_io_service_get_matching_services_bin: %s", e.what());
		if (errorString)
			CFRelease(errorString);

		return kIOReturnBadArgument;
	}
}

kern_return_t is_io_service_get_matching_services
(
	mach_port_t master_port,
	io_string_t matching,
	mach_port_t *existing
)
{
    // Old unsupported API
    return kIOReturnUnsupported;
}
