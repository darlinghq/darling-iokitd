#ifndef IOKIT_IOREGISTRYENTRY_H
#define IOKIT_IOREGISTRYENTRY_H
#include "IOObject.h"
#include <unordered_map>
#include <string>
#include <string_view>
#include <set>
#import <Foundation/NSDictionary.h>

class IORegistryEntry : public IOObject
{
public:
	virtual NSDictionary* getProperties();

	const char* className() const override;

	std::string_view getPathName(const char* plane);
	std::string getPath(const char* plane);
	std::set<IORegistryEntry*> getParents(const char* plane);
	std::set<IORegistryEntry*> getChildren(const char* plane);
	std::string getName(const char* plane = nullptr);

	static IORegistryEntry* root();
	
	void registerInPlane(const char* plane, const char* pathName, IORegistryEntry* parent);
protected:
	// The key is the plane name, e.g. kIOServicePlane (as declared in IOKit/IOKitKeys.h)
	std::unordered_map<std::string_view, std::string> m_name;
	std::unordered_map<std::string_view, std::set<IORegistryEntry*>> m_parents, m_children;
};

#endif
