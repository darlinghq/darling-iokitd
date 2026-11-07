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

#ifndef POWERASSERTIONSX11_H
#define POWERASSERTIONSX11_H
#include "PowerAssertions.h"

// Keeps the host display awake, which also keeps the host from suspending
// on idle (as kIOPMAssertPreventUserIdleDisplaySleep does on macOS)
class PowerAssertionPreventDisplaySleep : public PowerAssertion
{
protected:
	void activate() override;
	void deactivate() override;
private:
	bool m_screenSaverInhibited = false;
	bool m_disabledDPMS = false;
};

#endif

