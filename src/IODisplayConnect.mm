#include "IODisplayConnect.h"
#import <Foundation/NSString.h>

const char* IODisplayConnect::className() const
{
	return "IODisplayConnect";
}

NSDictionary* IODisplayConnect::matchingDictionary()
{
	const char* clsName = typeid(*this).name();
	return @{
		@"IOProviderClass": @"IODisplayConnect",
		@"IOClass": [NSString stringWithUTF8String: clsName]
	};
}

bool IODisplayConnect::conformsTo(const char* className)
{
	if (std::strcmp(className, "IODisplay") == 0)
		return true;
	return IOService::conformsTo(className);
}
