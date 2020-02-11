#include "IOIterator.h"
extern "C" {
#include "iokitmigServer.h"
}

IOIterator::IOIterator(const std::vector<IOObject*>& objects)
: m_objects(objects)
{

}

void IOIterator::reset()
{
	m_pos = 0;
}

const char* IOIterator::className() const
{
	return "IOUserIterator";
}

IOObject* IOIterator::next()
{
	if (m_pos >= m_objects.size())
		return nullptr;
	return m_objects[m_pos++];
}

kern_return_t is_io_iterator_next
(
	mach_port_t iterator,
	mach_port_t *object
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	*object = MACH_PORT_NULL;

	if (iter != nullptr)
	{
		IOObject* next = iter->next();
		if (next != nullptr)
		{
			*object = next->port();
			return KERN_SUCCESS;
		}
		else
			return KERN_SUCCESS;
	}
	else
		return KERN_INVALID_ARGUMENT;
}

kern_return_t is_io_iterator_reset
(
	mach_port_t iterator
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	if (!iter)
		return KERN_INVALID_ARGUMENT;
	else
	{
		iter->reset();
		return KERN_SUCCESS;
	}
}


kern_return_t is_io_iterator_is_valid
(
	mach_port_t iterator,
	boolean_t *is_valid
)
{
	IOIterator* iter = dynamic_cast<IOIterator*>(IOObject::lookup(iterator));
	*is_valid = iter != nullptr;
    return KERN_SUCCESS;
}
