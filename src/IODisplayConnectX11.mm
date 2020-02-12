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

void IODisplayConnectX11::discoverDevices(ServiceRegistry* targetServiceRegistry)
{
	static dispatch_once_t once;

	dispatch_once(&once, ^{
		m_root = new IORegistryEntry;
		m_root->registerInPlane(kIOServicePlane, "X11Display", IORegistryEntry::root());
	});

	if (!m_display)
	{
		m_display = XOpenDisplay(NULL);
    
		if (!m_display)
			m_display = XOpenDisplay(":0");
		if (!m_display)
			return;
	}

	int eventBase, errorBase;

	// Iterate through all outputs connected to the current screen
	if (XRRQueryExtension(m_display, &eventBase, &errorBase))
	{
		XRRScreenResources *screen = XRRGetScreenResources(m_display, DefaultRootWindow(m_display));

		Atom edidAtom = XInternAtom(m_display, "EDID", FALSE);

		for (int i = 0; i < screen->noutput; i++)
		{
			bool needFakeEDID = true;
			XRROutputInfo *oinfo = XRRGetOutputInfo(m_display, screen, screen->outputs[i]);
			NSMutableDictionary* props = [NSMutableDictionary dictionaryWithCapacity: 6];

			NSString* name = [[NSString alloc] initWithBytes: oinfo->name
												length: oinfo->nameLen
												encoding: NSUTF8StringEncoding];
			[name autorelease];

			[props setObject: @{ @"en_US": name }
					forKey: @(kDisplayProductName)];

			if (oinfo->crtc)
			{
				XRRCrtcInfo *crtc = XRRGetCrtcInfo(m_display, screen, oinfo->crtc);

				Atom actualType;
				unsigned long nitems, bytesAfter;
				int actualFormat;
				unsigned char* prop;

				// Getting the EDID property of various virtual displays succeeds, but returns 0 length data
				if (XRRGetOutputProperty(m_display, screen->outputs[i], edidAtom, 0, 100, FALSE, FALSE, AnyPropertyType, &actualType, &actualFormat, &nitems, &bytesAfter, &prop) == Success && nitems > 17)
				{
					[props setObject: [NSData dataWithBytes: prop length: nitems]
							forKey: @(kIODisplayEDIDKey)];
					
					uint32_t serial = CFSwapInt32LittleToHost(*(uint32_t*) (&prop[12]));
					[props setObject: [NSNumber numberWithInt: serial]
							forKey: @(kDisplaySerialNumber)];
					
					uint16_t model = CFSwapInt16LittleToHost(*(uint16_t*) (&prop[10]));
					[props setObject: [NSNumber numberWithInt: model]
							forKey: @(kDisplayProductID)];
					
					uint32_t vendor = CFSwapInt16BigToHost(*(uint16_t*) (&prop[8]));
					[props setObject: [NSNumber numberWithInt: vendor]
							forKey: @(kDisplayVendorID)];
					
					int week = prop[16];
					int year = int(prop[17]) + 1900;
					[props setObject: [NSNumber numberWithInt: week]
							forKey: @(kDisplayWeekOfManufacture)];
					[props setObject: [NSNumber numberWithInt: year]
							forKey: @(kDisplayYearOfManufacture)];

					needFakeEDID = false;
				}

				XRRFreeCrtcInfo(crtc);
			}

			if (needFakeEDID)
			{
				// generate a fake EDID, because otherwise IOKitUser will completely disregard our serial here
				uint8_t fakeEDID[128];

				memset(fakeEDID, 0, sizeof(fakeEDID));
				memset(fakeEDID+1, 0xff, 6);
				fakeEDID[12] = i+1;

				uint8_t csum = 0;
				for (int i = 0; i < sizeof(fakeEDID); i++)
					csum += fakeEDID[i];
				fakeEDID[127] = (256 - csum) % 256;

				[props setObject: [NSData dataWithBytes: fakeEDID length: sizeof(fakeEDID)]
						forKey: @(kIODisplayEDIDKey)];

				[props setObject: [NSNumber numberWithInt: i+1]
						forKey: @(kDisplaySerialNumber)];
			}

			IODisplayConnectX11* ioDisplay = new IODisplayConnectX11(i, props);
			targetServiceRegistry->registerService(ioDisplay);
			ioDisplay->registerInPlane(kIOServicePlane, oinfo->name, m_root);
			
			XRRFreeOutputInfo(oinfo);
		}

		XRRFreeScreenResources(screen);
	}
}
