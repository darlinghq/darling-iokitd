#ifndef IOKITD_IOOBJECT_H
#define IOKITD_IOOBJECT_H
#include <unordered_map>
#include <mach/mach.h>

class IOObject
{
public:
	// If userOwned is true, the object will be deallocated when there are no senders left
	IOObject(bool userOwned);
	virtual ~IOObject();

	virtual const char* className() const = 0;

	mach_port_t port() { return m_port; }
	static IOObject* lookup(mach_port_t port);
	static boolean_t deathNotify(mach_msg_header_t *request, mach_msg_header_t *reply);
private:
	mach_port_t m_port;
	static std::unordered_map<mach_port_t, IOObject*> m_objects;
};

#endif
