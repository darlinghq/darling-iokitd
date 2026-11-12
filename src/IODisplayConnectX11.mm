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

#include "IODisplayConnectX11.h"
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <CoreFoundation/CFByteOrder.h>
#include <dispatch/dispatch.h>
#include <cstdio>

Display* IODisplayConnectX11::m_display;
IORegistryEntry* IODisplayConnectX11::m_root;

IODisplayConnectX11::IODisplayConnectX11(int index, NSDictionary* props)
: m_index(index)
{
	m_props = [props retain];
}

IODisplayConnectX11::~IODisplayConnectX11()
{
	[m_props release];
}

NSDictionary* IODisplayConnectX11::getProperties()
{
	return m_props;
}

// Opens the connection to the X server once; without a server m_display stays NULL
void IODisplayConnectX11::openDisplay()
{
	if (m_display)
		return;

	m_display = XOpenDisplay(NULL);
	if (!m_display)
		m_display = XOpenDisplay(":0");
}

void IODisplayConnectX11::discoverDevices(ServiceRegistry* targetServiceRegistry, bool publishOutputs)
{
	static dispatch_once_t once;

	// The display is needed by other parts of iokitd (EGL, power assertions) even
	// when the displays themselves come from DRM
	openDisplay();
	if (!m_display || !publishOutputs)
		return;

	dispatch_once(&once, ^{
		m_root = new IORegistryEntry;
		m_root->registerInPlane(kIOServicePlane, "X11Display", IORegistryEntry::root());
	});

	int eventBase, errorBase;

	// Iterate through all outputs connected to the current screen
	if (XRRQueryExtension(m_display, &eventBase, &errorBase))
	{
		XRRScreenResources *screen = XRRGetScreenResources(m_display, DefaultRootWindow(m_display));
		if (!screen)
			return;

		Atom edidAtom = XInternAtom(m_display, "EDID", FALSE);

		for (int i = 0; i < screen->noutput; i++)
		{
			XRROutputInfo *oinfo = XRRGetOutputInfo(m_display, screen, screen->outputs[i]);
			if (!oinfo)
				continue;

			NSData* edid = nil;
			NSString* name = [[NSString alloc] initWithBytes: oinfo->name
												length: oinfo->nameLen
												encoding: NSUTF8StringEncoding];
			[name autorelease];

			if (oinfo->crtc)
			{
				XRRCrtcInfo *crtc = XRRGetCrtcInfo(m_display, screen, oinfo->crtc);

				Atom actualType;
				unsigned long nitems, bytesAfter;
				int actualFormat;
				unsigned char* prop = NULL;

				// Getting the EDID property of various virtual displays succeeds, but returns 0 length data
				if (XRRGetOutputProperty(m_display, screen->outputs[i], edidAtom, 0, 100, FALSE, FALSE, AnyPropertyType, &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success && prop)
				{
					if (nitems > 17)
						edid = [NSData dataWithBytes: prop length: nitems];
					XFree(prop);
				}

				if (crtc)
					XRRFreeCrtcInfo(crtc);
			}

			// the display's own name when the EDID has one, as for DRM displays
			NSDictionary* props = makeProperties(name, edid, i, true);
			IODisplayConnectX11* ioDisplay = new IODisplayConnectX11(i, props);
			targetServiceRegistry->registerService(ioDisplay);
			ioDisplay->registerInPlane(kIOServicePlane, oinfo->name, m_root);

			XRRFreeOutputInfo(oinfo);
		}

		XRRFreeScreenResources(screen);
	}
}
