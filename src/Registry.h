#ifndef IOKITD_REGISTRY_H
#define IOKITD_REGISTRY_H
#include "IOService.h"
#include <list>
#import <Foundation/NSDictionary.h>
#include "IOIterator.h"

class Registry
{
private:
	Registry() {}
public:
	static Registry* instance();
	void registerService(IOService* service);
	IOIterator* iteratorForMatchingServices(NSDictionary* criteria) const;
private:
	std::list<IOService*> m_registeredServices;
};

#endif
