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
