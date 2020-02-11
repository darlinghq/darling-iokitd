#include "IOService.h"

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

