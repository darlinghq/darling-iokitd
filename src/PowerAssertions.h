/*
 This file is part of Darling.

 Copyright (C) 2020 Lubos Dolezel

 Darling is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Darling is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef POWERASSERTIONS_H
#define POWERASSERTIONS_H
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <unordered_map>
#include <dispatch/dispatch.h>

void initPM();

// One kind of power assertion. The host is affected while at least one
// enabled assertion of the kind exists, in any process.
class PowerAssertion
{
public:
	void addRef();
	void delRef();
protected:
	virtual void activate() = 0;
	virtual void deactivate() = 0;
private:
	unsigned int m_numHolds = 0;
};

// Assertion types that are valid but have no host counterpart
class PowerAssertionNoHostEffect : public PowerAssertion
{
protected:
	void activate() override {}
	void deactivate() override {}
};

// Keeps the host from suspending on idle
class PowerAssertionPreventIdleSleep : public PowerAssertion
{
protected:
	void activate() override;
	void deactivate() override;
};

// The assertions of one process; they go away when the process exits.
class AppAssertions
{
private:
	AppAssertions(int pid);
	~AppAssertions();
public:
	static AppAssertions* get(int pid, bool create = true);

	struct HeldAssertion
	{
		int refcount;
		PowerAssertion* assertion;
		bool enabled;
		// Changes whenever a timeout is armed or the assertion is replaced
		uint64_t timeoutGeneration;
	};
	HeldAssertion* getAssertion(int assertionId);
	IOReturn createAssertion(PowerAssertion* which, bool enabled, int* assertionId);
	void setEnabled(HeldAssertion* assertion, bool enabled);
	void setTimeout(int assertionId, double seconds, CFStringRef action);
	void killAssertion(int assertionId);
private:
	int m_pid;
	std::unordered_map<int, HeldAssertion> m_heldAssertions;
	int m_nextAssertionNumber = 1;
	uint64_t m_nextTimeoutGeneration = 1;

	dispatch_source_t m_processSource;
	static std::unordered_map<int, AppAssertions*> m_instances;
};

#endif
