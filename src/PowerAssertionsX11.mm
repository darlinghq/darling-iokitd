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

#include "PowerAssertionsX11.h"
#include "IODisplayConnectX11.h"
#include <iostream>
#include <X11/Xlib.h>

#define BOOL	BOOL_THOU_SHALT_NOT_TYPEDEF_BOOL_IN_UNIMPORTANT_HEADERS
#include <X11/extensions/dpms.h>
#undef BOOL

void PowerAssertionPreventDisplaySleep::activate()
{
	Display* display = IODisplayConnectX11::getDisplay();
	if (!display)
		return;

	int eventBase, errorBase;
	if (DPMSQueryExtension(display, &eventBase, &errorBase))
	{
		DPMSDisable(display);
	}
}

void PowerAssertionPreventDisplaySleep::deactivate()
{
	Display* display = IODisplayConnectX11::getDisplay();
	if (!display)
		return;

	int eventBase, errorBase;
	if (DPMSQueryExtension(display, &eventBase, &errorBase))
	{
		DPMSEnable(display);
	}
}
