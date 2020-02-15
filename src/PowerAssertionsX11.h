#ifndef POWERASSERTIONSX11_H
#define POWERASSERTIONSX11_H
#include "PowerAssertions.h"

class PowerAssertionPreventDisplaySleep : public PowerAssertion
{
protected:
	void activate() override;
	void deactivate() override;
};

#endif

