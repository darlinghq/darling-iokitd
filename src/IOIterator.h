#ifndef IOKITD_IOITERATOR_H
#define IOKITD_IOITERATOR_H
#include "IOObject.h"
#include <vector>

class IOIterator : public IOObject
{
public:
	IOIterator(const std::vector<IOObject*>& objects);

	const char* className() const override;

	void reset();
	IOObject* next();
private:
	std::vector<IOObject*> m_objects;
	int m_pos = 0;
};

#endif

