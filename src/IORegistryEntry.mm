#include "IORegistryEntry.h"
#include <stack>
#include <IOKit/IOCFSerialize.h>
#include <cstring>
#include <vector>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <sstream>
#include "IOIterator.h"
extern "C" {
#include "iokitmigServer.h"
}

#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>

uint64_t IORegistryEntry::m_nextId = 0x100000000;
std::unordered_map<uint64_t, IORegistryEntry*> IORegistryEntry::m_entriesById;
std::shared_mutex IORegistryEntry::m_entriesByIdMutex;

IORegistryEntry::IORegistryEntry()
{
	std::unique_lock l(m_entriesByIdMutex);
	m_id = m_nextId++;
	m_entriesById.emplace(m_id, this);
}

IORegistryEntry::~IORegistryEntry()
{
	std::unique_lock l(m_entriesByIdMutex);
	m_entriesById.erase(m_id);
}

IORegistryEntry* IORegistryEntry::findById(uint64_t id)
{
	std::shared_lock l(m_entriesByIdMutex);
	auto it = m_entriesById.find(id);
	if (it == m_entriesById.end())
		return nullptr;
	
	IORegistryEntry* e = it->second;
	e->retain();
	return e;
}

NSDictionary* IORegistryEntry::getProperties()
{
	return @{};
}

const char* IORegistryEntry::className() const
{
	return "IORegistryEntry";
}

std::string_view IORegistryEntry::getPathName(const char* plane)
{
	auto it = m_name.find(plane);
	if (it != m_name.end())
		return it->second;
	return "?";
}

std::set<IORegistryEntry*> IORegistryEntry::getParents(const char* plane)
{
	auto it = m_parents.find(plane);
	if (it != m_parents.end())
		return it->second;
	return std::set<IORegistryEntry*>();
}

std::set<IORegistryEntry*> IORegistryEntry::getChildren(const char* plane)
{
	auto it = m_children.find(plane);
	if (it != m_children.end())
		return it->second;
	return std::set<IORegistryEntry*>();
}

void IORegistryEntry::registerInPlane(const char* plane, const char* pathName, IORegistryEntry* parent)
{
	m_name[plane] = pathName;

	auto it = m_parents.find(plane);

	if (it == m_parents.end())
		it = m_parents.insert(std::make_pair(std::string_view(plane), std::set<IORegistryEntry*>())).first;
	
	it->second.insert(parent);
	
	it = parent->m_children.find(plane);
	if (it == parent->m_children.end())
		it = parent->m_children.insert(std::make_pair(std::string_view(plane), std::set<IORegistryEntry*>())).first;

	it->second.insert(this);
}

std::string IORegistryEntry::getPath(const char* plane)
{
	std::stack<std::string> components;
	IORegistryEntry* cur = this;

	while (true)
	{
		std::set<IORegistryEntry*> parents = cur->getParents(plane);
		if (parents.empty())
		{
			if (cur != root())
				return "?unsup_plane?";

			break;
		}

		components.push(cur->getName(plane));
		
		IORegistryEntry* parent = *parents.begin();
		cur = parent;
	}

	std::string path = plane;
	path += ":";

	while (!components.empty())
	{
		path += "/";
		path += components.top();
		components.pop();
	}

	return path;
}

class RootIORegistryEntry : public IORegistryEntry
{
public:
	RootIORegistryEntry()
	{
		m_name.insert({ kIOServicePlane, "Root" });
	}
};

IORegistryEntry* IORegistryEntry::root()
{
	static std::shared_ptr<RootIORegistryEntry> e = std::make_shared<RootIORegistryEntry>();
	return e.get();
}

std::string IORegistryEntry::getName(const char* plane)
{
	if (plane)
	{
		auto it = m_name.find(plane);
		if (it != m_name.end())
			return it->second;
	}

	if (!m_name.empty())
		return m_name.begin()->second;
	return "?name?";
}

kern_return_t is_io_registry_entry_get_path
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_string_t path
)
{
    IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	std::string mypath = e->getPath(plane);
	strlcpy(path, mypath.c_str(), sizeof(io_string_t));

	return kIOReturnSuccess;
}

static void addEntries
(
	std::vector<IOObject*>& target,
	IORegistryEntry* e,
	const char* plane,
	bool children,
	bool recursively
)
{
	auto set = children ? e->getChildren(plane) : e->getParents(plane);

	target.insert(target.end(), set.begin(), set.end());

	if (recursively)
	{
		for (auto c : set)
			addEntries(target, c, plane, children, true);
	}
}

static mach_port_t ioRegistryCreateIterator
(
	IORegistryEntry* root,
	io_name_t plane,
	uint32_t options,
	bool includeRoot,
	bool children
)
{
	std::vector<IOObject*> objects;
	if (includeRoot)
		objects.push_back(root);

	// Depth-First Search
	addEntries(objects, root, plane, children, options & kIORegistryIterateRecursively);

	IOIterator* i = new IOIterator(objects);

	i->releaseLater();
	return i->port();
}

kern_return_t is_io_registry_entry_create_iterator
(
	mach_port_t registry_entry,
	io_name_t plane,
	uint32_t options,
	mach_port_t *iterator
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));

	*iterator = MACH_PORT_NULL;
	if (!e)
		return kIOReturnBadArgument;

	*iterator = ioRegistryCreateIterator(e, plane, options, true, true);
	return kIOReturnSuccess;
}

kern_return_t is_io_registry_create_iterator
(
	mach_port_t master_port,
	io_name_t plane,
	uint32_t options,
	mach_port_t *iterator
)
{
	*iterator = ioRegistryCreateIterator(IORegistryEntry::root(), plane, options, true, true);
	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_child_iterator
(
	mach_port_t registry_entry,
	io_name_t plane,
	mach_port_t *iterator
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));

	*iterator = MACH_PORT_NULL;
	if (!e)
		return kIOReturnBadArgument;

	*iterator = ioRegistryCreateIterator(e, plane, 0, false, true);
	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_parent_iterator
(
	mach_port_t registry_entry,
	io_name_t plane,
	mach_port_t *iterator
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));

	*iterator = MACH_PORT_NULL;
	if (!e)
		return kIOReturnBadArgument;

	*iterator = ioRegistryCreateIterator(e, plane, 0, false, false);
	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_name_in_plane
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t name
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	std::string entryName = e->getName(plane);
	strncpy(name, entryName.c_str(), sizeof(io_name_t) - 1);
	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_properties_bin
(
	mach_port_t registry_entry,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	NSDictionary* props = e->getProperties();

	CFDataRef data = IOCFSerialize(props, kIOCFSerializeToBinary);

	*propertiesCnt = CFDataGetLength(data);
	kern_return_t kr = vm_allocate(mach_task_self(), (vm_address_t*) properties, *propertiesCnt, true);

	if (kr == kIOReturnSuccess)
		memcpy(*properties, CFDataGetBytePtr(data), *propertiesCnt);

	CFRelease(data);

    return kr;
}

kern_return_t is_io_registry_entry_get_properties
(
	mach_port_t registry_entry,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	// Old unsupported API
    return kIOReturnUnsupported;
}

kern_return_t is_io_registry_get_root_entry
(
	mach_port_t master_port,
	mach_port_t *root
)
{
    *root = IORegistryEntry::root()->port();
    return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_name
(
	mach_port_t registry_entry,
	io_name_t name
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	std::string sname = e->getName();
	strlcpy(name, sname.c_str(), sizeof(io_name_t));

    return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_registry_entry_id
(
	mach_port_t registry_entry,
	uint64_t *entry_id
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;
	
	*entry_id = e->id();
    return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_get_location_in_plane
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t location
)
{
	// Plane locations are optional
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_from_path
(
	mach_port_t master_port,
	io_string_t path,
	mach_port_t *registry_entry
)
{
	*registry_entry = 0;

	std::vector<std::string> components;
    std::istringstream iss(path);

    for (std::string token; std::getline(iss, token, '/'); )
	{
		if (!token.empty())
        	components.push_back(std::move(token));
	}
    
	if (components.empty())
		return kIOReturnBadArgument;
	
	std::string& planeName = components[0];
	if (planeName[planeName.length()-1] != ':')
		return kIOReturnBadArgument;
	else
		planeName.resize(planeName.length()-1); // remove the final colon

	IORegistryEntry* current = IORegistryEntry::root();
	for (size_t i = 1; i < components.size(); i++)
	{
		auto children = current->getChildren(planeName.c_str());
		IORegistryEntry* next = nullptr;

		for (IORegistryEntry* e : children)
		{
			if (strcasecmp(e->getName(planeName.c_str()).c_str(), components[i].c_str()) == 0)
			{
				next = e;
				break;
			}
		}

		if (!next)
			return kIOReturnNoDevice;
		current = next;
	}

	*registry_entry = current->port();

	return kIOReturnSuccess;
}

static NSDictionary* findKey(IORegistryEntry* e, const char* plane, NSString* key)
{
	NSDictionary* props = e->getProperties();
	id value = [props objectForKey: key];

	if (value != nil)
		return @{ key: value };
	
	auto set = e->getChildren(plane);
	for (IORegistryEntry* c : set)
	{
		NSDictionary* rv = findKey(c, plane, key);
		if (rv != nil)
			return rv;
	}

	return nil;
}

kern_return_t is_io_registry_entry_get_property_bin
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t property_name,
	uint32_t options,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	// TODO: compare property_name against kIORegistryEntryPropertyKeysKey, but IOKitUser doesn't seem to use it

	*propertiesCnt = 0;
	*properties = nullptr;

	NSDictionary* retval;

	NSString* propName = [NSString stringWithUTF8String: property_name];

	if ((options & kIORegistryIterateRecursively) && plane[0])
	{
		// Recursive search for the property
		auto set = e->getChildren(plane);
		retval = findKey(e, plane, propName);

		if (!retval)
			return kIOReturnError; // TODO: or another code?
	}
	else
	{
		NSDictionary* props = e->getProperties();
		
		id value = [props objectForKey: propName];

		if (!value)
			return kIOReturnError; // TODO: or another code?

		retval = @{ propName: value };
	}

	CFDataRef data = IOCFSerialize(retval, kIOCFSerializeToBinary);
	*propertiesCnt = CFDataGetLength(data);
	kern_return_t kr = vm_allocate(mach_task_self(), (vm_address_t*) properties, *propertiesCnt, true);

	if (kr == kIOReturnSuccess)
		memcpy(*properties, CFDataGetBytePtr(data), *propertiesCnt);

	CFRelease(data);

    return kr;
}

