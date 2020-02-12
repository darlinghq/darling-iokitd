#ifndef IOKITD_IODISPLAYCONNECT_H
#define IOKITD_IODISPLAYCONNECT_H

#include "IOService.h"

class IODisplayConnect : public IOService
{
public:
	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	bool conformsTo(const char* className) override;
};

#endif
