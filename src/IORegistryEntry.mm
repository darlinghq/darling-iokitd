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
#include <CoreFoundation/CFNumber.h>

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
	{
		std::unique_lock l(m_entriesByIdMutex);
		m_entriesById.erase(m_id);
	}

	// Unlink this entry in both directions, so that walking a plane later
	// never reaches a deleted entry. Registry requests and object teardown
	// all run on the main queue.
	for (auto& [plane, parents] : m_parents)
	{
		for (IORegistryEntry* parent : parents)
			parent->m_children[plane].erase(this);
	}
	for (auto& [plane, children] : m_children)
	{
		for (IORegistryEntry* child : children)
			child->m_parents[plane].erase(this);
	}
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

bool IORegistryEntry::registerInPlane(const char* plane, const char* pathName, IORegistryEntry* parent)
{
	// Refuse to attach the entry below itself or below one of its own
	// descendants: a plane must stay acyclic.
	std::set<IORegistryEntry*> visited;
	std::vector<IORegistryEntry*> pending { parent };

	while (!pending.empty())
	{
		IORegistryEntry* cur = pending.back();
		pending.pop_back();

		if (cur == this)
			return false;
		if (!visited.insert(cur).second)
			continue;

		for (IORegistryEntry* p : cur->getParents(plane))
			pending.push_back(p);
	}

	m_name[plane] = pathName;
	m_parents[plane].insert(parent);
	parent->m_children[plane].insert(this);

	return true;
}

void IORegistryEntry::detachFromPlane(const char* plane)
{
	auto parents = m_parents.find(plane);

	if (parents != m_parents.end())
	{
		for (IORegistryEntry* parent : parents->second)
			parent->m_children[plane].erase(this);
		m_parents.erase(parents);
	}
}

std::optional<std::string> IORegistryEntry::getPath(const char* plane)
{
	std::stack<std::string> components;
	IORegistryEntry* cur = this;

	while (true)
	{
		std::set<IORegistryEntry*> parents = cur->getParents(plane);
		if (parents.empty())
		{
			if (cur != root())
				return std::nullopt;

			break;
		}

		components.push(cur->getName(plane));
		
		IORegistryEntry* parent = *parents.begin();
		cur = parent;
	}

	std::string path = plane;
	path += ":";

	// The root itself is "plane:/"
	if (components.empty())
		path += "/";

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

	std::optional<std::string> mypath = e->getPath(plane);
	if (!mypath)
		return kIOReturnBadArgument;
	if (mypath->size() >= sizeof(io_string_t))
		return kIOReturnNoSpace;

	strlcpy(path, mypath->c_str(), sizeof(io_string_t));

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

// Replies with the binary serialization of 'value' out of line.
//
// device.defs declares these replies "physicalcopy" without "dealloc", so
// sending the reply copies the buffer and leaves it with us. Requests are
// served one at a time on the main queue and each reply is sent before the
// next request runs, so the previous reply's buffer can be released here.
static kern_return_t serializeOutOfLine(id value, io_buf_ptr_t *properties, mach_msg_type_number_t *propertiesCnt)
{
	static vm_address_t lastReply;
	static vm_size_t lastReplySize;

	*properties = nullptr;
	*propertiesCnt = 0;

	if (lastReply)
	{
		vm_deallocate(mach_task_self(), lastReply, lastReplySize);
		lastReply = 0;
	}

	CFDataRef data = IOCFSerialize((CFTypeRef) value, kIOCFSerializeToBinary);
	if (!data)
		return kIOReturnNoMemory;

	mach_msg_type_number_t size = CFDataGetLength(data);
	kern_return_t kr = vm_allocate(mach_task_self(), (vm_address_t*) properties, size, true);

	if (kr == KERN_SUCCESS)
	{
		memcpy(*properties, CFDataGetBytePtr(data), size);
		*propertiesCnt = size;
		lastReply = (vm_address_t) *properties;
		lastReplySize = size;
	}
	else
		*properties = nullptr;

	CFRelease(data);
	return kr;
}

kern_return_t is_io_registry_entry_get_properties_bin
(
	mach_port_t registry_entry,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	*properties = nullptr;
	*propertiesCnt = 0;

	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	return serializeOutOfLine(e->getProperties(), properties, propertiesCnt);
}

kern_return_t is_io_registry_entry_get_properties_bin_buf
(
	mach_port_t registry_entry,
	mach_vm_address_t buf,
	mach_vm_size_t *bufsize,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	// 'buf' lives in the caller's address space. The caller reads the reply
	// from there only when no out-of-line data comes back, so always
	// answer out of line.
	return is_io_registry_entry_get_properties_bin(registry_entry, properties, propertiesCnt);
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

static IOReturn lookupPath(const char* path, IORegistryEntry** entry)
{
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

	*entry = current;

	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_from_path
(
	mach_port_t master_port,
	io_string_t path,
	mach_port_t *registry_entry
)
{
	*registry_entry = 0;

	IORegistryEntry* entry;
	IOReturn ret = lookupPath(path, &entry);
	if (ret != kIOReturnSuccess)
		return ret;

	*registry_entry = entry->port();

	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_from_path_ool
(
	mach_port_t master_port,
	io_string_inband_t path,
	io_buf_ptr_t path_ool,
	mach_msg_type_number_t path_oolCnt,
	kern_return_t *result,
	mach_port_t *registry_entry
)
{
	*registry_entry = MACH_PORT_NULL;

	// IOKitUser sends paths longer than io_string_inband_t out of line, with
	// the terminating NUL, and leaves the inband path empty.
	const char* cpath = nullptr;
	if (path[0])
	{
		if (memchr(path, 0, sizeof(io_string_inband_t)))
			cpath = path;
	}
	else if (path_ool && path_oolCnt && !path_ool[path_oolCnt - 1])
		cpath = path_ool;

	IORegistryEntry* entry;
	*result = cpath ? lookupPath(cpath, &entry) : kIOReturnBadArgument;
	if (*result == kIOReturnSuccess)
		*registry_entry = entry->port();

	// The out-of-line buffer was mapped into this task for this request.
	if (path_ool)
		vm_deallocate(mach_task_self(), (vm_address_t) path_ool, path_oolCnt);

	return KERN_SUCCESS;
}

kern_return_t is_io_registry_entry_get_path_ool
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_string_inband_t path,
	io_buf_ptr_t *path_ool,
	mach_msg_type_number_t *path_oolCnt
)
{
	*path_ool = nullptr;
	*path_oolCnt = 0;

	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	// Registry paths here are a few components deep, so the inband buffer
	// always suffices; an out-of-line reply is not implemented.
	std::optional<std::string> mypath = e->getPath(plane);
	if (!mypath)
		return kIOReturnBadArgument;
	if (mypath->size() >= sizeof(io_string_inband_t))
		return kIOReturnNoSpace;

	strlcpy(path, mypath->c_str(), sizeof(io_string_inband_t));

	return kIOReturnSuccess;
}

kern_return_t is_io_registry_entry_in_plane
(
	mach_port_t registry_entry,
	io_name_t plane,
	boolean_t *inPlane
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	*inPlane = !e->getParents(plane).empty();
	return kIOReturnSuccess;
}

// Finds 'key' on the entry or, with kIORegistryIterateRecursively, on the
// first entry below it in 'plane' that has it.
static id findProperty(IORegistryEntry* e, const char* plane, NSString* key, bool recursively)
{
	id value = [e->getProperties() objectForKey: key];

	if (value != nil || !recursively)
		return value;

	for (IORegistryEntry* c : e->getChildren(plane))
	{
		value = findProperty(c, plane, key, true);
		if (value != nil)
			return value;
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
	*propertiesCnt = 0;
	*properties = nullptr;

	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	// TODO: compare property_name against kIORegistryEntryPropertyKeysKey, but IOKitUser doesn't seem to use it

	NSString* propName = [NSString stringWithUTF8String: property_name];
	if (!propName)
		return kIOReturnBadArgument;

	bool recursively = (options & kIORegistryIterateRecursively) && plane[0];
	id value = findProperty(e, plane, propName, recursively);

	if (!value)
		return kIOReturnNotFound;

	// The reply is the value itself, which IORegistryEntryCreateCFProperty()
	// returns as is.
	return serializeOutOfLine(value, properties, propertiesCnt);
}

kern_return_t is_io_registry_entry_get_property_bin_buf
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t property_name,
	uint32_t options,
	mach_vm_address_t buf,
	mach_vm_size_t *bufsize,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
	// See is_io_registry_entry_get_properties_bin_buf()
	return is_io_registry_entry_get_property_bin(registry_entry, plane, property_name, options, properties, propertiesCnt);
}

kern_return_t is_io_registry_entry_get_property_bytes
(
	mach_port_t registry_entry,
	io_name_t property_name,
	io_struct_inband_t data,
	mach_msg_type_number_t *dataCnt
)
{
	IORegistryEntry* e = dynamic_cast<IORegistryEntry*>(IOObject::lookup(registry_entry));
	if (!e)
		return kIOReturnBadArgument;

	CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, property_name, kCFStringEncodingUTF8);
	if (!key)
		return kIOReturnBadArgument;

	CFTypeRef value = CFDictionaryGetValue((CFDictionaryRef) e->getProperties(), key);
	CFRelease(key);
	if (!value)
		return kIOReturnNoResources;

	const void* bytes;
	CFIndex length;
	char string[sizeof(io_struct_inband_t)];
	int64_t number;
	CFTypeID type = CFGetTypeID(value);

	if (type == CFDataGetTypeID())
	{
		bytes = CFDataGetBytePtr((CFDataRef) value);
		length = CFDataGetLength((CFDataRef) value);
	}
	else if (type == CFStringGetTypeID())
	{
		// Only fails when the string and its NUL do not fit the reply
		if (!CFStringGetCString((CFStringRef) value, string, sizeof(string), kCFStringEncodingUTF8))
			return kIOReturnIPCError;
		bytes = string;
		length = strlen(string) + 1;
	}
	else if (type == CFBooleanGetTypeID())
	{
		bool yes = CFBooleanGetValue((CFBooleanRef) value);
		bytes = yes ? "Yes" : "No";
		length = yes ? sizeof("Yes") : sizeof("No");
	}
	else if (type == CFNumberGetTypeID() && !CFNumberIsFloatType((CFNumberRef) value))
	{
		// The low-order bytes come first on a little-endian guest
		CFNumberGetValue((CFNumberRef) value, kCFNumberSInt64Type, &number);
		bytes = &number;
		length = CFNumberGetByteSize((CFNumberRef) value);
	}
	else
		return kIOReturnBadArgument;

	if (*dataCnt < length)
		return kIOReturnIPCError;

	*dataCnt = length;
	memcpy(data, bytes, length);
	return kIOReturnSuccess;
}
