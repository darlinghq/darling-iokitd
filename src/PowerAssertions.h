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

class PowerAssertion
{
public:
	// How many AppAssertions instances reference this assertion (*not* a sum of references within all these AppAssertions instances)
	void addRef();
	void delRef();
protected:
	virtual void activate() = 0;
	virtual void deactivate() = 0;
private:
	int m_numHoldingApps = 0;
};

class AppAssertions
{
private:
	AppAssertions(int pid);
	~AppAssertions();
public:
	static AppAssertions* get(int pid);

	struct HeldAssertion
	{
		int refcount;
		PowerAssertion* assertion;
	};
	HeldAssertion* getAssertion(int assertionId);
	int createAssertion(PowerAssertion* which);
	void killAssertion(int assertionId);
private:
	std::unordered_map<int, HeldAssertion> m_heldAssertions;
	int m_nextAssertionNumber = 1;

	dispatch_source_t m_processSource;
	static std::unordered_map<int, AppAssertions*> m_instances;
};

#endif
