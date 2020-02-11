#include "IOObject.h"
#include "iokitd.h"
#include <stdexcept>
#include <cstring>
#include <os/log.h>
#include <dispatch/private.h>
extern "C" {
#include "iokitmigServer.h"
}

std::unordered_map<mach_port_t, IOObject*> IOObject::m_objects;

IOObject::IOObject()
{
	auto task = mach_task_self();
	kern_return_t kr;

	kr = mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &m_port);
	if (kr != KERN_SUCCESS)
		throw std::runtime_error("Failed to allocate Mach port");

	// -> we have refcount of 1
	kr = mach_port_insert_right(task, m_port, m_port, MACH_MSG_TYPE_MAKE_SEND);
	if (kr != KERN_SUCCESS)
		throw std::runtime_error("Failed to add Mach port send right");

	m_dispatchSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, m_port, 0, dispatch_get_main_queue());

	if (!m_dispatchSource)
		throw std::runtime_error("Failed to create dispatch source");

	dispatch_source_set_event_handler(m_dispatchSource, ^{
		dispatch_mig_server(m_dispatchSource, is_iokit_subsystem.maxsize, iokit_server);
	});
	dispatch_resume(m_dispatchSource);

	mach_port_t	oldTargetOfNotification	= MACH_PORT_NULL;
	kr = mach_port_request_notification(task, m_port, MACH_NOTIFY_NO_SENDERS, 1, g_deathPort, MACH_MSG_TYPE_MAKE_SEND_ONCE, &oldTargetOfNotification);
	if (kr != KERN_SUCCESS)
		throw std::runtime_error("Failed to setup MACH_NOTIFY_NO_SENDERS");

	m_objects.insert(std::make_pair(m_port, this));
}

IOObject::~IOObject()
{
	if (m_dispatchSource)
		dispatch_release(m_dispatchSource);

	m_objects.erase(m_port);

	mach_port_deallocate(mach_task_self(), m_port);
}

void IOObject::retain()
{
	mach_port_mod_refs(mach_task_self(), m_port, MACH_PORT_RIGHT_SEND, 1);
}

void IOObject::release()
{
	mach_port_mod_refs(mach_task_self(), m_port, MACH_PORT_RIGHT_SEND, -1);
}

void IOObject::releaseLater()
{
	dispatch_async(dispatch_get_main_queue(), ^{ release(); });
}

IOObject* IOObject::lookup(mach_port_t port)
{
	auto it = m_objects.find(port);
	if (it != m_objects.end())
		return it->second;
	return nullptr;
}

bool IOObject::conformsTo(const char* className)
{
	return std::strcmp(className, this->className());
}

boolean_t IOObject::deathNotify(mach_msg_header_t *request, mach_msg_header_t *reply)
{
	mach_no_senders_notification_t* Request = (mach_no_senders_notification_t*) request;
	mig_reply_error_t* Reply = (mig_reply_error_t*) reply;

	reply->msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request->msgh_bits), 0);
	reply->msgh_remote_port = request->msgh_remote_port;
	reply->msgh_size = sizeof(mig_reply_error_t);
	reply->msgh_local_port = MACH_PORT_NULL;
	reply->msgh_id = request->msgh_id + 100;

	if (Request->not_header.msgh_id == MACH_NOTIFY_NO_SENDERS)
	{
		IOObject* object = lookup(request->msgh_remote_port);

		if (object != nullptr)
			delete object;
		else
			os_log_error(OS_LOG_DEFAULT, "Received MACH_NOTIFY_NO_SENDERS for port %d, which matches no objects", request->msgh_remote_port);

		Reply->Head.msgh_bits = 0;
		Reply->Head.msgh_remote_port = MACH_PORT_NULL;
		Reply->RetCode = KERN_SUCCESS;
		return true;
	}
	else
	{
		Reply->NDR     = NDR_record;
		Reply->RetCode = MIG_BAD_ID;
		return false;
	}
}

kern_return_t is_io_object_get_class
(
	mach_port_t object,
	io_name_t className
)
{
	IOObject* o = IOObject::lookup(object);
	if (!o)
		return KERN_INVALID_ARGUMENT;

	strlcpy(className, o->className(), sizeof(io_name_t));
    return KERN_SUCCESS;
}

kern_return_t is_io_object_conforms_to
(
	mach_port_t object,
	io_name_t className,
	boolean_t *conforms
)
{
    IOObject* o = IOObject::lookup(object);
	if (!o)
		return KERN_INVALID_ARGUMENT;

	*conforms = o->conformsTo(className);
	return KERN_SUCCESS;
}
