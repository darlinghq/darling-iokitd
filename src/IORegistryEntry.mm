#include "IORegistryEntry.h"
#include <stack>
#include <IOCFSerialize.h>
#include <cstring>
extern "C" {
#include "iokitmigServer.h"
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
		
		IORegistryEntry* parent = *parents.begin();
		components.push(parent->getPath(plane));
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

IORegistryEntry* IORegistryEntry::root()
{
	static IORegistryEntry e;
	return &e;
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
		return KERN_INVALID_ARGUMENT;

	std::string mypath = e->getPath(plane);
	strlcpy(path, mypath.c_str(), sizeof(io_string_t));

	return KERN_SUCCESS;
}

kern_return_t is_io_registry_entry_create_iterator
(
	mach_port_t registry_entry,
	io_name_t plane,
	uint32_t options,
	mach_port_t *iterator
)
{
    puts("STUB is_io_registry_entry_create_iterator");
    return KERN_NOT_SUPPORTED;
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
		return KERN_INVALID_ARGUMENT;

	NSDictionary* props = e->getProperties();

	CFDataRef data = IOCFSerialize(props, kIOCFSerializeToBinary);

	*propertiesCnt = CFDataGetLength(data);
	kern_return_t kr = vm_allocate(mach_task_self(), (vm_address_t*) properties, *propertiesCnt, true);

	if (kr == KERN_SUCCESS)
	{
		memcpy(*properties, CFDataGetBytePtr(data), *propertiesCnt);
	}

	CFRelease(data);

    return kr;
}



// STUB called: is_io_registry_entry_get_property_bin
// STUB called: is_io_registry_entry_create_iterator
// STUB called: is_io_registry_entry_get_properties_bin
// STUB called: is_io_registry_get_root_entry
// STUB called: is_io_registry_entry_from_path
