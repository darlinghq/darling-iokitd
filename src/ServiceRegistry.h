#ifndef IOKITD_REGISTRY_H
#define IOKITD_REGISTRY_H
#include "IOService.h"
#include <list>
#import <Foundation/NSDictionary.h>
#include "IOIterator.h"

class ServiceRegistry
{
private:
	ServiceRegistry() {}
public:
	static ServiceRegistry* instance();
	void registerService(IOService* service);
	IOIterator* iteratorForMatchingServices(NSDictionary* criteria) const;
private:
	std::list<IOService*> m_registeredServices;
};

#endif
