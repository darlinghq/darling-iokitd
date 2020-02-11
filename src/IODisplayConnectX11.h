#ifndef IOKITD_IODISPLAYCONNECTX11_H
#define IOKITD_IODISPLAYCONNECTX11_H
#include "IODisplayConnect.h"
#include "Registry.h"
#include <vector>
#include <X11/Xlib.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>

class IODisplayConnectX11 : IODisplayConnect
{
private:
	IODisplayConnectX11(int index, NSDictionary* props);
public:
	~IODisplayConnectX11();
	static void discoverDevices(Registry* targetRegistry);
private:
	static Display* m_display;
	// Index of the corresponding XrandR output
	int m_index;

	NSDictionary* m_props;
};

#endif
