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

#include "PowerAssertions.h"
#include "PowerAssertionsX11.h"
#include <stdexcept>
#include <iostream>
#include <string>
#include <CoreFoundation/CoreFoundation.h>
#include <bsm/libbsm.h>
#import <Foundation/NSString.h>

extern "C" {
#include "powermanagementServer.h"
}

std::unordered_map<int, AppAssertions*> AppAssertions::m_instances;
static std::unordered_map<std::string, PowerAssertion*> g_assertionKinds;

void initPM()
{
	NSString* str = (NSString*) kIOPMAssertPreventUserIdleDisplaySleep;
	g_assertionKinds.insert(std::make_pair([str UTF8String], new PowerAssertionPreventDisplaySleep));
}

static PowerAssertion* getByType(NSString* type)
{
	auto it = g_assertionKinds.find([type UTF8String]);
	if (it != g_assertionKinds.end())
		return it->second;
	return nullptr;
}

void PowerAssertion::addRef()
{
	if (m_numHoldingApps == 0)
		activate();
	m_numHoldingApps++;
}

void PowerAssertion::delRef()
{
	m_numHoldingApps--;
	if (m_numHoldingApps == 0)
		deactivate();
}

AppAssertions::AppAssertions(int pid)
{
	m_processSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, pid, DISPATCH_PROC_EXIT, dispatch_get_main_queue());
	if (!m_processSource)
		throw std::runtime_error("dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC) failed");

	if (m_processSource)
	{
		dispatch_source_set_event_handler(m_processSource, ^{
			m_instances.erase(pid);
			delete this;
		});
		dispatch_resume(m_processSource);
	}
}

AppAssertions::~AppAssertions()
{
	for (auto& [assertionId, ha] : m_heldAssertions)
		ha.assertion->delRef();
		
	if (m_processSource != nullptr)
		dispatch_release(m_processSource);
}

AppAssertions* AppAssertions::get(int pid)
{
	auto it = m_instances.find(pid);
	if (it != m_instances.end())
		return it->second;
	
	try
	{
		AppAssertions* aa = new AppAssertions(pid);
		m_instances.insert(std::make_pair(pid, aa));
		return aa;
	}
	catch (const std::exception& e)
	{
		std::cerr << "AppAssertions::get() failed for PID " << pid << ": " << e.what() << std::endl;
		return nullptr;
	}
}

AppAssertions::HeldAssertion* AppAssertions::getAssertion(int assertionId)
{
	auto it = m_heldAssertions.find(assertionId);
	if (it != m_heldAssertions.end())
		return &it->second;
	return nullptr;
}

int AppAssertions::createAssertion(PowerAssertion* which)
{
	int number = m_nextAssertionNumber++;
	HeldAssertion held = { 1, which };
	m_heldAssertions.insert(std::make_pair(number, held));
	which->addRef();
	return number;
}

void AppAssertions::killAssertion(int assertionId)
{
	auto it = m_heldAssertions.find(assertionId);
	if (it == m_heldAssertions.end())
		return;

	it->second.assertion->delRef();
	m_heldAssertions.erase(it);
}

kern_return_t _io_pm_assertion_create
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *assertion_id,
	int *disableAppSleep,
	int *enTrIntensity,
	int *return_code
)
{
	if (!props)
		return KERN_INVALID_ARGUMENT;

	CFDataRef data = CFDataCreateWithBytesNoCopy(nullptr, (const UInt8*) props, propsCnt, kCFAllocatorNull);
	CFDictionaryRef properties = (CFDictionaryRef) CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);

	CFRelease(data);

	vm_deallocate(mach_task_self(), props, propsCnt);

	if (!properties)
		return KERN_INVALID_ARGUMENT;
	
	if (CFGetTypeID(properties) != CFDictionaryGetTypeID())
	{
		std::cerr << "Unexpected properties data type\n";
		CFRelease(properties);
		return KERN_INVALID_ARGUMENT;
	}

	CFStringRef cftype = (CFStringRef) CFDictionaryGetValue(properties, kIOPMAssertionTypeKey);

	if (!cftype || CFStringGetTypeID() != CFGetTypeID(cftype))
	{
		std::cerr << "kIOPMAssertionTypeKey is not a string\n";
		CFRelease(properties);
		return KERN_INVALID_ARGUMENT;
	}

	PowerAssertion* pa = getByType((NSString*) cftype);
	if (pa == nullptr)
	{
		std::cerr << "Unsupported assertion type\n";
		CFRelease(properties);
		return KERN_INVALID_ARGUMENT;
	}

	pid_t callerPid;
	audit_token_to_au32(token, nullptr, nullptr, nullptr, nullptr, nullptr, &callerPid, nullptr, nullptr);

	// std::cout << "Creating assertion for PID " << callerPid << std::endl;
	*assertion_id = AppAssertions::get(callerPid)->createAssertion(pa);
	*return_code = 0;
	
    return KERN_SUCCESS;
}

kern_return_t _io_pm_assertion_retain_release
(
	mach_port_t server,
	audit_token_t token,
	int assertion_id,
	int action,
	int *retainCnt,
	int *disableAppSleep,
	int *enableAppSleep,
	int *return_code
)
{
	pid_t callerPid;
	audit_token_to_au32(token, nullptr, nullptr, nullptr, nullptr, nullptr, &callerPid, nullptr, nullptr);

	AppAssertions* appAssertions = AppAssertions::get(callerPid);
	AppAssertions::HeldAssertion* ha = appAssertions->getAssertion(assertion_id);

	if (!ha)
	{
		std::cerr << "_io_pm_assertion_retain_release() cannot find assertion " << assertion_id << std::endl;
		return KERN_INVALID_ARGUMENT;
	}

	*return_code = 0;
	if (action == kIOPMAssertionMIGDoRetain)
	{
		*retainCnt = ++ha->refcount;
	}
	else if (action == kIOPMAssertionMIGDoRelease)
	{
		*retainCnt = --ha->refcount;
		if (ha->refcount == 0)
			appAssertions->killAssertion(assertion_id);
	}
	else
		return KERN_INVALID_ARGUMENT;

    return KERN_NOT_SUPPORTED;
}
