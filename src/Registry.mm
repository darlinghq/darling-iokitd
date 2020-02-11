#include "iokitd.h"
#include "Registry.h"
#include <IOCFUnserialize.h>
#include <CoreFoundation/CFString.h>
#include <os/log.h>
#include <stdexcept>
#include "IOIterator.h"

extern "C" {
#include "iokitmigServer.h"
}

Registry* Registry::instance()
{
	static Registry reg;
	return &reg;
}

IOIterator* Registry::iteratorForMatchingServices(NSDictionary* criteria) const
{
	std::vector<IOObject*> matching;

	for (auto it = m_registeredServices.begin(); it != m_registeredServices.end(); it++)
	{
		if ((*it)->matches(criteria))
			matching.push_back(*it);
	}

	return new IOIterator(matching);
}

void Registry::registerService(IOService* service)
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

		// Criteria example:
		// IOProviderClass -> IODisplayConnect
		IOIterator* iterator = Registry::instance()->iteratorForMatchingServices((NSDictionary*) criteria);
		CFShow(criteria);
		CFRelease(criteria);

		*existing = iterator->port();

		iterator->releaseLater();

		return KERN_SUCCESS;
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, e.what());
		if (errorString)
			CFRelease(errorString);

		return KERN_INVALID_ARGUMENT;
	}
}
