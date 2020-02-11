#ifndef IOKITD_IODISPLAYCONNECTX11_H
#define IOKITD_IODISPLAYCONNECTX11_H
#include "IODisplayConnect.h"
#include "ServiceRegistry.h"
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
	static void discoverDevices(ServiceRegistry* targetRegistry);
	NSDictionary* getProperties() override;
private:
	static Display* m_display;
	// Index of the corresponding XrandR output
	int m_index;

	NSDictionary* m_props;
	static IORegistryEntry* m_root;
};

#endif
