#ifndef IOKITD_REGISTRY_H
#define IOKITD_REGISTRY_H
#include "IOService.h"

class Registry
{
public:
	void registerService(IOService* service);
};

#endif
