#ifndef IOKITD_IOSERVICE_H
#define IOKITD_IOSERVICE_H
#include <Foundation/NSDictionary.h>
#include "IOObject.h"

class IOService : public IOObject
{
public:
	IOService();
	virtual NSDictionary* matchingDictionary() = 0;
	bool matches(NSDictionary* dict);
};

#endif
