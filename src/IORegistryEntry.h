#ifndef IOKIT_IOREGISTRYENTRY_H
#define IOKIT_IOREGISTRYENTRY_H
#include "IOObject.h"
#include <unordered_map>
#include <string>
#include <string_view>
#include <set>
#include <shared_mutex>
#import <Foundation/NSDictionary.h>

class IORegistryEntry : public IOObject
{
public:
	IORegistryEntry();
	~IORegistryEntry();
	virtual NSDictionary* getProperties();

	const char* className() const override;

	std::string_view getPathName(const char* plane);
	std::string getPath(const char* plane);
	std::set<IORegistryEntry*> getParents(const char* plane);
	std::set<IORegistryEntry*> getChildren(const char* plane);
	std::string getName(const char* plane = nullptr);
	uint64_t id() const { return m_id; }

	static IORegistryEntry* root();

	// This call adds a reference!
	static IORegistryEntry* findById(uint64_t id);
	
	void registerInPlane(const char* plane, const char* pathName, IORegistryEntry* parent);
protected:
	// The key is the plane name, e.g. kIOServicePlane (as declared in IOKit/IOKitKeys.h)
	std::unordered_map<std::string_view, std::string> m_name;
	std::unordered_map<std::string_view, std::set<IORegistryEntry*>> m_parents, m_children;
	uint64_t m_id;

	static std::unordered_map<uint64_t, IORegistryEntry*> m_entriesById;
	static uint64_t m_nextId;
	static std::shared_mutex m_entriesByIdMutex;
};

#endif
