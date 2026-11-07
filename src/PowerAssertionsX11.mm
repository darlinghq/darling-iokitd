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
#include "HostPowerInhibit.h"
#include <X11/Xlib.h>

#define BOOL	BOOL_THOU_SHALT_NOT_TYPEDEF_BOOL_IN_UNIMPORTANT_HEADERS
#include <X11/extensions/dpms.h>
#undef BOOL

void PowerAssertionPreventDisplaySleep::activate()
{
	iokitd_idle_inhibit_acquire();

	// The desktop's screen saver service also works on Wayland. Without one,
	// turn off X11 display power management while the assertion is held.
	m_screenSaverInhibited = iokitd_screensaver_inhibit_acquire();
	if (m_screenSaverInhibited)
		return;
	iokitd_screensaver_inhibit_release();

	Display* display = IODisplayConnectX11::getDisplay();
	if (!display)
		return;

	int eventBase, errorBase;
	CARD16 powerLevel;
	BOOL_THOU_SHALT_NOT_TYPEDEF_BOOL_IN_UNIMPORTANT_HEADERS enabled;
	if (DPMSQueryExtension(display, &eventBase, &errorBase) && DPMSInfo(display, &powerLevel, &enabled) && enabled)
	{
		// Only undo what we changed.
		DPMSDisable(display);
		XFlush(display);
		m_disabledDPMS = true;
	}
}

void PowerAssertionPreventDisplaySleep::deactivate()
{
	if (m_screenSaverInhibited)
		iokitd_screensaver_inhibit_release();
	m_screenSaverInhibited = false;

	if (m_disabledDPMS)
	{
		Display* display = IODisplayConnectX11::getDisplay();
		if (display)
		{
			DPMSEnable(display);
			XFlush(display);
		}
		m_disabledDPMS = false;
	}

	iokitd_idle_inhibit_release();
}
