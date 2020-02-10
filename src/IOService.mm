#include "IOService.h"

IOService::IOService()
: IOObject(false)
{

}

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

