#ifndef IOKITD_IOSERVICE_H
#define IOKITD_IOSERVICE_H
#include <Foundation/NSDictionary.h>
#include "IORegistryEntry.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitLibPrivate.h>

class IOService : public IORegistryEntry
{
public:
	IOService();
	virtual NSDictionary* matchingDictionary() = 0;
	bool matches(NSDictionary* dict);
	bool conformsTo(const char* className) override;

	uint32_t busyState() const { return m_busyState; }
	uint64_t accumulatedBusyTime() const { return m_accumulatedBusyTime; }
	uint64_t state() const { return m_state; }
protected:
	uint32_t m_busyState = 0;
	uint64_t m_accumulatedBusyTime = 0;
	uint64_t m_state = kIOServiceRegisteredState | kIOServiceMatchedState;
};

#endif
