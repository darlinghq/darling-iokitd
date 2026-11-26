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
#include "HostPowerInhibit.h"
#include <climits>
#include <cmath>
#include <stdexcept>
#include <string>
#include <set>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLibPrivate.h>
#include <bsm/libbsm.h>
#include <mach/mach.h>
#include <notify.h>
#include <os/log.h>
#import <Foundation/NSString.h>

extern "C" {
#include "powermanagementServer.h"
}

std::unordered_map<int, AppAssertions*> AppAssertions::m_instances;
static std::unordered_map<std::string, PowerAssertion*> g_assertionKinds;
// The legacy names of the types as the current names of the same types, for the status listing.
static std::unordered_map<std::string, std::string> g_currentTypeNames;
// Clients that asked for assertion change notifications with io_pm_assertion_notify, by notification name.
static std::unordered_map<std::string, int> g_notifyRegistrations;

static std::string stringOf(CFStringRef string)
{
	return [(NSString*) string UTF8String];
}

static void postNotification(const char* name)
{
	auto it = g_notifyRegistrations.find(name);
	if (it != g_notifyRegistrations.end() && it->second > 0)
		notify_post(name);
}

void initPM()
{
	static PowerAssertionPreventIdleSleep idle;
	static PowerAssertionPreventDisplaySleep display;
	static PowerAssertionNoHostEffect other;

	// The legacy and current names of a type share one kind.
	g_assertionKinds = {
		{ [(NSString*) kIOPMAssertionTypeNoIdleSleep UTF8String], &idle },
		{ [(NSString*) kIOPMAssertPreventUserIdleSystemSleep UTF8String], &idle },
		{ [(NSString*) kIOPMAssertionTypeNoDisplaySleep UTF8String], &display },
		{ [(NSString*) kIOPMAssertPreventUserIdleDisplaySleep UTF8String], &display },
		{ [(NSString*) kIOPMAssertionTypePreventSystemSleep UTF8String], &other },
		{ [(NSString*) kIOPMAssertPreventDiskIdle UTF8String], &other },
		{ [(NSString*) kIOPMAssertNetworkClientActive UTF8String], &other },
	};
	g_currentTypeNames = {
		{ stringOf(kIOPMAssertionTypeNoIdleSleep), stringOf(kIOPMAssertPreventUserIdleSystemSleep) },
		{ stringOf(kIOPMAssertPreventUserIdleSystemSleep), stringOf(kIOPMAssertPreventUserIdleSystemSleep) },
		{ stringOf(kIOPMAssertionTypeNoDisplaySleep), stringOf(kIOPMAssertPreventUserIdleDisplaySleep) },
		{ stringOf(kIOPMAssertPreventUserIdleDisplaySleep), stringOf(kIOPMAssertPreventUserIdleDisplaySleep) },
		{ stringOf(kIOPMAssertionTypePreventSystemSleep), stringOf(kIOPMAssertionTypePreventSystemSleep) },
		{ stringOf(kIOPMAssertPreventDiskIdle), stringOf(kIOPMAssertPreventDiskIdle) },
		{ stringOf(kIOPMAssertNetworkClientActive), stringOf(kIOPMAssertNetworkClientActive) },
	};
}

// The system-wide levels changed (see kIOPMAssertionsChangedNotifyString)
void PowerAssertion::addRef()
{
	if (m_numHolds++ == 0)
	{
		activate();
		postNotification(kIOPMAssertionsChangedNotifyString);
	}
}

void PowerAssertion::delRef()
{
	if (m_numHolds > 0 && --m_numHolds == 0)
	{
		deactivate();
		postNotification(kIOPMAssertionsChangedNotifyString);
	}
}

void PowerAssertionPreventIdleSleep::activate()
{
	if (!iokitd_idle_inhibit_acquire())
		os_log(OS_LOG_DEFAULT, "iokitd: could not take a host idle inhibitor");
}

void PowerAssertionPreventIdleSleep::deactivate()
{
	iokitd_idle_inhibit_release();
}

AppAssertions::AppAssertions(int pid)
: m_pid(pid)
{
	m_processSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, pid, DISPATCH_PROC_EXIT, dispatch_get_main_queue());
	if (!m_processSource)
		throw std::runtime_error("dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC) failed");

	dispatch_source_set_event_handler(m_processSource, ^{
		m_instances.erase(pid);
		delete this;
	});
	dispatch_resume(m_processSource);
}

AppAssertions::~AppAssertions()
{
	for (auto& [assertionId, held] : m_heldAssertions)
	{
		if (held.enabled)
			held.assertion->delRef();
	}

	dispatch_source_cancel(m_processSource);
	dispatch_release(m_processSource);
	if (!m_heldAssertions.empty())
		postAnyChanged();
}

void AppAssertions::postAnyChanged()
{
	postNotification(kIOPMAssertionsAnyChangedNotifyString);
}

void AppAssertions::forEach(void (^visit)(AppAssertions* app))
{
	for (auto& [pid, app] : m_instances)
		visit(app);
}

AppAssertions* AppAssertions::get(int pid, bool create)
{
	auto it = m_instances.find(pid);
	if (it != m_instances.end())
		return it->second;
	if (!create)
		return nullptr;

	try
	{
		AppAssertions* aa = new AppAssertions(pid);
		m_instances.insert(std::make_pair(pid, aa));
		return aa;
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: cannot track assertions of pid %d: %s", pid, e.what());
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

IOReturn AppAssertions::createAssertion(PowerAssertion* which, bool enabled, int* assertionId,
	const std::string& type, const std::string& name)
{
	if (m_nextAssertionNumber == INT_MAX)
		return kIOReturnNoResources;

	int number = m_nextAssertionNumber++;
	m_heldAssertions.insert(std::make_pair(number, HeldAssertion{ 1, which, enabled, 0, type, name }));
	if (enabled)
		which->addRef();
	*assertionId = number;
	postAnyChanged();
	return kIOReturnSuccess;
}

CFDictionaryRef AppAssertions::copyAssertionInfo(int assertionId, const HeldAssertion& held) const
{
	CFMutableDictionaryRef info = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	int level = held.enabled ? kIOPMAssertionLevelOn : kIOPMAssertionLevelOff;
	CFStringRef type = CFStringCreateWithCString(nullptr, held.type.c_str(), kCFStringEncodingUTF8);
	CFNumberRef levelNumber = CFNumberCreate(nullptr, kCFNumberIntType, &level);
	CFNumberRef pidNumber = CFNumberCreate(nullptr, kCFNumberIntType, &m_pid);
	CFNumberRef idNumber = CFNumberCreate(nullptr, kCFNumberIntType, &assertionId);

	if (type)
		CFDictionarySetValue(info, kIOPMAssertionTypeKey, type);
	// The legacy names of the types are listed as given, with the current name of the type beside them.
	auto current = g_currentTypeNames.find(held.type);
	if (current != g_currentTypeNames.end())
	{
		CFStringRef trueType = CFStringCreateWithCString(nullptr, current->second.c_str(), kCFStringEncodingUTF8);
		if (trueType)
		{
			CFDictionarySetValue(info, kIOPMAssertionTrueTypeKey, trueType);
			CFRelease(trueType);
		}
	}
	CFDictionarySetValue(info, kIOPMAssertionLevelKey, levelNumber);
	CFDictionarySetValue(info, kIOPMAssertionPIDKey, pidNumber);
	CFDictionarySetValue(info, kIOPMAssertionIdKey, idNumber);
	if (!held.name.empty())
	{
		CFStringRef name = CFStringCreateWithCString(nullptr, held.name.c_str(), kCFStringEncodingUTF8);
		if (name)
		{
			CFDictionarySetValue(info, kIOPMAssertionNameKey, name);
			CFRelease(name);
		}
	}
	if (type)
		CFRelease(type);
	CFRelease(levelNumber);
	CFRelease(pidNumber);
	CFRelease(idNumber);
	return info;
}

void AppAssertions::setEnabled(HeldAssertion* held, bool enabled)
{
	if (held->enabled == enabled)
		return;
	held->enabled = enabled;
	if (enabled)
		held->assertion->addRef();
	else
		held->assertion->delRef();
	postAnyChanged();
}

void AppAssertions::setTimeout(int assertionId, double seconds, CFStringRef action)
{
	HeldAssertion* held = getAssertion(assertionId);
	if (!held)
		return;

	uint64_t generation = m_nextTimeoutGeneration++;
	held->timeoutGeneration = generation;

	bool release = action && CFEqual(action, kIOPMAssertionTimeoutActionRelease);
	bool turnOff = !action || CFEqual(action, kIOPMAssertionTimeoutActionTurnOff);
	int pid = m_pid;

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		// The process may have exited, or the assertion may be gone or rearmed.
		AppAssertions* app = AppAssertions::get(pid, false);
		HeldAssertion* current = app ? app->getAssertion(assertionId) : nullptr;
		if (!current || current->timeoutGeneration != generation)
			return;

		os_log(OS_LOG_DEFAULT, "iokitd: power assertion %d of pid %d timed out", assertionId, pid);
		if (release)
			app->killAssertion(assertionId);
		else if (turnOff)
			app->setEnabled(current, false);
	});
}

void AppAssertions::killAssertion(int assertionId)
{
	auto it = m_heldAssertions.find(assertionId);
	if (it == m_heldAssertions.end())
		return;

	if (it->second.enabled)
		it->second.assertion->delRef();
	m_heldAssertions.erase(it);
	postAnyChanged();
}

static pid_t callerPid(audit_token_t token)
{
	pid_t pid;
	audit_token_to_au32(token, nullptr, nullptr, nullptr, nullptr, nullptr, &pid, nullptr, nullptr);
	return pid;
}

// Takes over the out-of-line serialized object. Returns it or NULL.
static CFTypeRef copyObject(vm_offset_t props, mach_msg_type_number_t propsCnt)
{
	CFTypeRef object = nullptr;

	if (props && propsCnt)
	{
		CFDataRef data = CFDataCreateWithBytesNoCopy(nullptr, (const UInt8*) props, propsCnt, kCFAllocatorNull);
		if (data)
		{
			object = CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
			CFRelease(data);
		}
	}
	if (props)
		vm_deallocate(mach_task_self(), props, propsCnt);
	return object;
}

// Takes over the out-of-line properties data. Returns a dictionary or NULL.
static CFDictionaryRef copyProperties(vm_offset_t props, mach_msg_type_number_t propsCnt)
{
	CFDictionaryRef properties = (CFDictionaryRef) copyObject(props, propsCnt);

	if (properties && CFGetTypeID(properties) != CFDictionaryGetTypeID())
	{
		CFRelease(properties);
		properties = nullptr;
	}
	return properties;
}

// kIOPMAssertionLevelKey: kIOPMAssertionLevelOff disables the assertion and
// any other number enables it. Returns false if the value is not a number.
static bool levelIsOn(CFTypeRef value, bool* on)
{
	int level;

	if (CFGetTypeID(value) != CFNumberGetTypeID() || !CFNumberGetValue((CFNumberRef) value, kCFNumberIntType, &level))
		return false;
	*on = level != kIOPMAssertionLevelOff;
	return true;
}

// kIOPMAssertionTimeoutKey; 0 means no timeout.
static bool timeoutSeconds(CFTypeRef value, double* seconds)
{
	if (CFGetTypeID(value) != CFNumberGetTypeID() || !CFNumberGetValue((CFNumberRef) value, kCFNumberDoubleType, seconds))
		return false;
	return std::isfinite(*seconds) && *seconds >= 0;
}

static CFStringRef timeoutAction(CFDictionaryRef properties)
{
	CFTypeRef action = CFDictionaryGetValue(properties, kIOPMAssertionTimeoutActionKey);
	if (action && CFGetTypeID(action) == CFStringGetTypeID())
		return (CFStringRef) action;
	return nullptr;
}

// The MIG routines report errors in return_code; the transport always
// succeeds, so that clients get the IOReturn rather than an internal error.

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
	*assertion_id = kIOPMNullAssertionID;
	*disableAppSleep = 0;
	*enTrIntensity = 0;
	*return_code = kIOReturnBadArgument;

	CFDictionaryRef properties = copyProperties(props, propsCnt);
	if (!properties)
		return KERN_SUCCESS;

	CFTypeRef type = CFDictionaryGetValue(properties, kIOPMAssertionTypeKey);
	CFTypeRef level = CFDictionaryGetValue(properties, kIOPMAssertionLevelKey);
	CFTypeRef timeout = CFDictionaryGetValue(properties, kIOPMAssertionTimeoutKey);
	bool enabled = true;
	double seconds = 0;

	// An unknown type is kIOReturnBadArgument, as observed on macOS.
	auto kind = g_assertionKinds.end();
	if (type && CFGetTypeID(type) == CFStringGetTypeID())
		kind = g_assertionKinds.find([(NSString*) type UTF8String]);

	if (kind != g_assertionKinds.end()
		&& (!level || levelIsOn(level, &enabled))
		&& (!timeout || timeoutSeconds(timeout, &seconds)))
	{
		AppAssertions* app = AppAssertions::get(callerPid(token));
		CFTypeRef name = CFDictionaryGetValue(properties, kIOPMAssertionNameKey);
		std::string nameString;
		if (name && CFGetTypeID(name) == CFStringGetTypeID())
			nameString = stringOf((CFStringRef) name);
		*return_code = app ? app->createAssertion(kind->second, enabled, assertion_id,
			stringOf((CFStringRef) type), nameString) : kIOReturnNoResources;

		if (*return_code == kIOReturnSuccess && seconds > 0)
			app->setTimeout(*assertion_id, seconds, timeoutAction(properties));
	}

	CFRelease(properties);
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
	*retainCnt = 0;
	*disableAppSleep = 0;
	*enableAppSleep = 0;
	// An ID the caller does not hold is kIOReturnBadArgument, as observed on macOS.
	*return_code = kIOReturnBadArgument;

	AppAssertions* appAssertions = AppAssertions::get(callerPid(token), false);
	AppAssertions::HeldAssertion* ha = appAssertions ? appAssertions->getAssertion(assertion_id) : nullptr;
	if (!ha)
		return KERN_SUCCESS;

	if (action == kIOPMAssertionMIGDoRetain)
	{
		if (ha->refcount == INT_MAX)
		{
			*return_code = kIOReturnNoResources;
			return KERN_SUCCESS;
		}
		*retainCnt = ++ha->refcount;
	}
	else if (action == kIOPMAssertionMIGDoRelease)
	{
		*retainCnt = --ha->refcount;
		if (ha->refcount == 0)
			appAssertions->killAssertion(assertion_id);
	}
	else
		return KERN_SUCCESS;

	*return_code = kIOReturnSuccess;
	return KERN_SUCCESS;
}

kern_return_t _io_pm_assertion_set_properties
(
	mach_port_t server,
	audit_token_t token,
	int assertion_id,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *disableAppSleep,
	int *enableAppSleep,
	int *enTrIntensity,
	int *return_code
)
{
	*disableAppSleep = 0;
	*enableAppSleep = 0;
	*enTrIntensity = 0;
	*return_code = kIOReturnBadArgument;

	CFDictionaryRef properties = copyProperties(props, propsCnt);
	if (!properties)
		return KERN_SUCCESS;

	AppAssertions* app = AppAssertions::get(callerPid(token), false);
	AppAssertions::HeldAssertion* held = app ? app->getAssertion(assertion_id) : nullptr;
	CFTypeRef level = CFDictionaryGetValue(properties, kIOPMAssertionLevelKey);
	CFTypeRef timeout = CFDictionaryGetValue(properties, kIOPMAssertionTimeoutKey);
	bool enabled = held ? held->enabled : false;
	double seconds = 0;

	// Other properties (names, details) are descriptive and accepted as they are.
	if (held && (!level || levelIsOn(level, &enabled)) && (!timeout || timeoutSeconds(timeout, &seconds)))
	{
		app->setEnabled(held, enabled);
		if (timeout)
		{
			if (seconds > 0)
				app->setTimeout(assertion_id, seconds, timeoutAction(properties));
			else
				held->timeoutGeneration = 0;
		}
		*return_code = kIOReturnSuccess;
	}

	CFRelease(properties);
	return KERN_SUCCESS;
}

// The serialized object as out-of-line data for a MIG reply. NULL object: no data.
static IOReturn replyWithObject(CFTypeRef object, vm_offset_t* data, mach_msg_type_number_t* dataCnt)
{
	*data = 0;
	*dataCnt = 0;

	CFDataRef serialized = CFPropertyListCreateData(nullptr, object, kCFPropertyListBinaryFormat_v1_0, 0, nullptr);
	if (!serialized)
		return kIOReturnInternalError;

	CFIndex length = CFDataGetLength(serialized);
	vm_address_t address = 0;
	if (vm_allocate(mach_task_self(), &address, length, VM_FLAGS_ANYWHERE) != KERN_SUCCESS)
	{
		CFRelease(serialized);
		return kIOReturnNoMemory;
	}
	memcpy((void*) address, CFDataGetBytePtr(serialized), length);
	CFRelease(serialized);
	*data = address;
	*dataCnt = (mach_msg_type_number_t) length;
	return kIOReturnSuccess;
}

// The listing of every process' assertions in the form io_pm_assertion_copy_details answers
// kIOPMAssertionMIGCopyAll and kIOPMAssertionMIGCopyInactive with: an array with one dictionary
// per process, holding the pid and the array of its assertions.
static CFArrayRef copyAssertionsByProcess(bool inactive)
{
	CFMutableArrayRef processes = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);

	AppAssertions::forEach(^(AppAssertions* app) {
		CFMutableArrayRef list = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
		for (auto& [assertionId, held] : app->held())
		{
			if (held.enabled == inactive)
				continue;
			CFDictionaryRef info = app->copyAssertionInfo(assertionId, held);
			CFArrayAppendValue(list, info);
			CFRelease(info);
		}
		if (CFArrayGetCount(list) > 0)
		{
			int pid = app->pid();
			CFNumberRef pidNumber = CFNumberCreate(nullptr, kCFNumberIntType, &pid);
			CFMutableDictionaryRef entry = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			CFDictionarySetValue(entry, kIOPMAssertionPIDKey, pidNumber);
			CFDictionarySetValue(entry, CFSTR("PerTaskAssertions"), list);
			CFArrayAppendValue(processes, entry);
			CFRelease(entry);
			CFRelease(pidNumber);
		}
		CFRelease(list);
	});
	return processes;
}

// The current type names with 1 for a type some process holds and 0 for one nobody does (as macOS lists them).
static CFDictionaryRef copyAssertionsStatus()
{
	__block std::set<std::string> raised;

	AppAssertions::forEach(^(AppAssertions* app) {
		for (auto& [assertionId, held] : app->held())
		{
			auto current = g_currentTypeNames.find(held.type);
			if (held.enabled && current != g_currentTypeNames.end())
				raised.insert(current->second);
		}
	});

	CFMutableDictionaryRef status = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	std::set<std::string> types;
	for (auto& [legacy, current] : g_currentTypeNames)
		types.insert(current);
	for (auto& type : types)
	{
		int level = raised.count(type) ? 1 : 0;
		CFStringRef key = CFStringCreateWithCString(nullptr, type.c_str(), kCFStringEncodingUTF8);
		CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &level);
		CFDictionarySetValue(status, key, number);
		CFRelease(key);
		CFRelease(number);
	}
	return status;
}

// The assertions with a type, current or legacy name, as an array of assertion dictionaries.
static CFArrayRef copyAssertionsByType(CFStringRef type)
{
	CFMutableArrayRef list = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
	std::string wanted = stringOf(type);
	auto wantedCurrent = g_currentTypeNames.find(wanted);

	AppAssertions::forEach(^(AppAssertions* app) {
		for (auto& [assertionId, held] : app->held())
		{
			auto current = g_currentTypeNames.find(held.type);
			bool same = held.type == wanted
				|| (wantedCurrent != g_currentTypeNames.end() && current != g_currentTypeNames.end()
					&& current->second == wantedCurrent->second);
			if (!same)
				continue;
			CFDictionaryRef info = app->copyAssertionInfo(assertionId, held);
			CFArrayAppendValue(list, info);
			CFRelease(info);
		}
	});
	return list;
}

// IOPMCopyAssertionsByProcess, IOPMCopyInactiveAssertionsByProcess, IOPMCopyAssertionsStatus,
// IOPMCopyAssertionsByType and IOPMAssertionCopyProperties. The scheduled power events are not
// kept by iokitd.
kern_return_t _io_pm_assertion_copy_details
(
	mach_port_t server,
	audit_token_t token,
	int assertion_id,
	int whichData,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	vm_offset_t *assertions,
	mach_msg_type_number_t *assertionsCnt,
	int *return_val
)
{
	CFTypeRef selector = copyObject(props, propsCnt);
	__block CFTypeRef result = nullptr;

	*assertions = 0;
	*assertionsCnt = 0;
	*return_val = kIOReturnBadArgument;

	switch (whichData)
	{
		case kIOPMAssertionMIGCopyAll:
			result = copyAssertionsByProcess(false);
			break;
		case kIOPMAssertionMIGCopyInactive:
			result = copyAssertionsByProcess(true);
			break;
		case kIOPMAssertionMIGCopyStatus:
			result = copyAssertionsStatus();
			break;
		case kIOPMAssertionMIGCopyByType:
			if (selector && CFGetTypeID(selector) == CFStringGetTypeID())
				result = copyAssertionsByType((CFStringRef) selector);
			break;
		case kIOPMAssertionMIGCopyOneAssertionProperties:
			AppAssertions::forEach(^(AppAssertions* app) {
				AppAssertions::HeldAssertion* held = app->getAssertion(assertion_id);
				if (held && !result)
					result = app->copyAssertionInfo(assertion_id, *held);
			});
			if (!result)
				*return_val = kIOReturnNotFound;
			break;
		default:
			break;
	}

	if (result)
	{
		*return_val = replyWithObject(result, assertions, assertionsCnt);
		CFRelease(result);
	}
	if (selector)
		CFRelease(selector);
	return KERN_SUCCESS;
}

// IOPMAssertionNotify: the clients that want kIOPMAssertionsChangedNotifyString and
// kIOPMAssertionsAnyChangedNotifyString posted register with it first.
kern_return_t _io_pm_assertion_notify
(
	mach_port_t server,
	audit_token_t token,
	string_t name,
	int req_type,
	int *return_code
)
{
	*return_code = kIOReturnBadArgument;

	if (strcmp(name, kIOPMAssertionsChangedNotifyString) != 0
		&& strcmp(name, kIOPMAssertionsAnyChangedNotifyString) != 0
		&& strcmp(name, kIOPMAssertionTimedOutNotifyString) != 0)
		return KERN_SUCCESS;

	if (req_type == kIOPMNotifyRegister)
		g_notifyRegistrations[name]++;
	else if (req_type == kIOPMNotifyDeRegister)
	{
		int& count = g_notifyRegistrations[name];
		if (count > 0)
			count--;
	}
	else
		return KERN_SUCCESS;

	*return_code = kIOReturnSuccess;
	return KERN_SUCCESS;
}
