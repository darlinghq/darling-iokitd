#ifndef IOKITD_IODISPLAYCONNECT_H
#define IOKITD_IODISPLAYCONNECT_H

#include "IOService.h"

class IODisplayConnect : public IOService
{
public:
	NSDictionary* matchingDictionary() override;
};

#endif