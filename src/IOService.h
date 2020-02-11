#ifndef IOKITD_IOSERVICE_H
#define IOKITD_IOSERVICE_H
#include <Foundation/NSDictionary.h>
#include "IORegistryEntry.h"

class IOService : public IORegistryEntry
{
public:
	IOService();
	virtual NSDictionary* matchingDictionary() = 0;
	bool matches(NSDictionary* dict);
};

#endif
