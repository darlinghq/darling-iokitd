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
#include "IOSurfaceRoot.h"
#import <Foundation/NSString.h>
#include "iokitd.h"
#include "IOSurface.h"
#include "IODisplayConnectX11.h"
#include <IOKit/IOCFUnserialize.h>
#include <stdexcept>
#include <iostream>

IOSurfaceRoot::IOSurfaceRoot()
{
	m_display = eglGetDisplay(IODisplayConnectX11::getDisplay());

    if (m_display == EGL_NO_DISPLAY)
        return;

	EGLConfig config;
	int num_config;

	EGLint const attribute_list[] = {
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_NONE
	};

    eglInitialize(m_display, NULL, NULL);
    eglChooseConfig(m_display, attribute_list, &config, 1, &num_config);

    eglBindAPI(EGL_OPENGL_API);
}

IOSurfaceRoot::~IOSurfaceRoot()
{
	eglTerminate(m_display);
}

IOSurfaceRoot* IOSurfaceRoot::instance()
{
	static IOSurfaceRoot instance;
	return &instance;
}

void IOSurfaceRoot::registerSelf(ServiceRegistry* targetServiceRegistry)
{
	targetServiceRegistry->registerService(instance());
	instance()->registerInPlane(kIOServicePlane, "IOSurfaceRoot", IORegistryEntry::root());
}

const char* IOSurfaceRoot::className() const
{
	return "IOSurfaceRoot";
}

NSDictionary* IOSurfaceRoot::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOResources",
		@"IOClass": @"IOSurfaceRoot",
		@"IOMatchCategory": @"IOSurfaceRoot",
		@"IOResourceMatch": @"IOBSD",
	};
}

IOExternalMethod *IOSurfaceRoot::getExternalMethodForIndex(UInt32 index)
{
	static const IOExternalMethod methods[] = {
		[kIOSurfaceMethodCreate] = {
			.func = IOMethod(&IOSurfaceRoot::createSurface),
			.flags = kIOUCStructIStructO,
			.count0 = kIOUCVariableStructureSize,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodRelease] = {
			.func = IOMethod(&IOSurfaceRoot::releaseSurface),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodLock] = {
			.func = IOMethod(&IOSurfaceRoot::lockSurface),
			.flags = kIOUCStructIStructO,
			.count0 = 8,
			.count1 = 0,
		},
		[kIOSurfaceMethodUnlock] = {
			.func = IOMethod(&IOSurfaceRoot::unlockSurface),
			.flags = kIOUCStructIStructO,
			.count0 = 8,
			.count1 = 0,
		},
		[kIOSurfaceMethodLookupByID] = {
			.func = IOMethod(&IOSurfaceRoot::lookupById),
			.flags = kIOUCScalarIStructO,
			.count0 = 1,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodIncrementUseCount] = {
			.func = IOMethod(&IOSurfaceRoot::incrementSurfaceUseCount),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodDecrementUseCount] = {
			.func = IOMethod(&IOSurfaceRoot::decrementSurfaceUseCount),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 0,
		},
		[kIOSurfaceMethodCreateByMachPort] = {
			.func = IOMethod(&IOSurfaceRoot::lookupByMachPort),
			.flags = kIOUCScalarIStructO,
			.count0 = 1,
			.count1 = kIOUCVariableStructureSize,
		},
		[kIOSurfaceMethodCreateMachPort] = {
			.func = IOMethod(&IOSurfaceRoot::getSurfaceMachPort),
			.flags = kIOUCScalarIScalarO,
			.count0 = 1,
			.count1 = 1,
		},
	};

	registerCaller();

	if (index >= sizeof(methods)/sizeof(IOExternalMethod))
		return nullptr;

	static IOExternalMethod method = methods[index];
	method.object = this;
	return &method;
}

IOReturn IOSurfaceRoot::createSurface(const void* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	if (!hasDisplay())
		return KERN_FAILURE;
		
	CFStringRef errorString;
	CFTypeRef cf = IOCFUnserializeBinary(static_cast<const char*>(inputData), inputCount, nullptr, 0, &errorString);

	if (!cf)
	{
		CFShow(errorString);
		CFRelease(errorString);
		return KERN_INVALID_ARGUMENT;
	}
	if (CFGetTypeID(cf) != CFDictionaryGetTypeID())
	{
		CFRelease(cf);
		return KERN_INVALID_ARGUMENT;
	}

	IOReturn ret = KERN_SUCCESS;
	
	try
	{
		IOSurface* surface = new IOSurface(m_nextSurfaceID++, (NSDictionary*) cf);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error in IOSurfaceRoot::createSurface(): " << e.what() << std::endl;
		ret = KERN_INVALID_ARGUMENT;
	}

	CFRelease(cf);
	return ret;
}

IOReturn IOSurfaceRoot::releaseSurface(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::unlockSurface(const _IOSurfaceLockUnlock* inputData, void* outputData, IOByteCount inputCount, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lookupById(IOSurfaceID surfaceID, void* outputData, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::incrementSurfaceUseCount(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::decrementSurfaceUseCount(IOSurfaceID surfaceID)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::lookupByMachPort(mach_port_t remoteMachPort, void* outputData, IOByteCount* outputCount)
{
	return KERN_NOT_SUPPORTED;
}

IOReturn IOSurfaceRoot::getSurfaceMachPort(IOSurfaceID surfaceID, mach_port_t* remoteMachPort)
{
	return KERN_NOT_SUPPORTED;
}

void IOSurfaceRoot::registerCaller()
{
	if (m_processes.find(g_iokitCurrentCallerPID) != m_processes.end())
		return; // Already registered

	RegisteredProcess proc;
	const pid_t caller = g_iokitCurrentCallerPID;
	proc.pid = caller;
	proc.procSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, caller, DISPATCH_PROC_EXIT, dispatch_get_main_queue());

	dispatch_source_set_event_handler(proc.procSource, ^{
		callerDied(caller);
		dispatch_source_cancel(proc.procSource);
	});
	dispatch_resume(proc.procSource);

	m_processes.emplace(proc.pid, std::move(proc));
}

void IOSurfaceRoot::callerDied(pid_t pid)
{
	auto it = m_processes.find(pid);
	if (it == m_processes.end())
		return; // Shouldn't happen

	RegisteredProcess& proc = it->second;
	dispatch_release(proc.procSource);

	for (auto& [surfaceID, mapping] : proc.surfaces)
	{
		auto itSurface = m_surfaces.find(surfaceID);
		if (itSurface == m_surfaces.end())
			continue;

		while (mapping.useCount > 0)
		{
			itSurface->second->decrementUseCount();
			mapping.useCount--;
		}

		itSurface->second->release();
	}

	m_processes.erase(it);
}

void IOSurfaceRoot::surfaceDestroyed(IOSurfaceID myId)
{
	m_surfaces.erase(myId);
}
