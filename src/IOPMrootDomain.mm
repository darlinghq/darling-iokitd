/*
 This file is part of Darling.

 Copyright (C) 2026 Darling Developers

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

#include "IOPMrootDomain.h"
#include "IOInterestNotification.h"
#include "LogindSleep.h"
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLibDefs.h>
#include <dispatch/dispatch.h>
#include <os/log.h>
#include <cstdlib>
#include <cstring>
#import <Foundation/NSString.h>

// The IOPower plane parent of the root domain, as on macOS
class IOPowerConnection : public IORegistryEntry
{
public:
	const char* className() const override { return "IOPowerConnection"; }
};

IOPMrootDomain* IOPMrootDomain::instance()
{
	static IOPMrootDomain* domain = new IOPMrootDomain;
	return domain;
}

void IOPMrootDomain::registerSelf(ServiceRegistry* targetServiceRegistry)
{
	IOPMrootDomain* domain = instance();
	static IOPowerConnection* connection = new IOPowerConnection;

	targetServiceRegistry->registerService(domain);
	domain->registerInPlane(kIOServicePlane, "IOPMrootDomain", IORegistryEntry::root());
	connection->registerInPlane(kIOPowerPlane, "IOPowerConnection", IORegistryEntry::root());
	domain->registerInPlane(kIOPowerPlane, "IOPMrootDomain", connection);
}

const char* IOPMrootDomain::className() const
{
	return "IOPMrootDomain";
}

NSDictionary* IOPMrootDomain::matchingDictionary()
{
	return @{
		@"IOProviderClass": @"IOPMrootDomain",
		@"IOClass": @"IOPMrootDomain",
	};
}

NSDictionary* IOPMrootDomain::getProperties()
{
	return @{
		@"IOClass": @"IOPMrootDomain",
		@"IOUserClientClass": @"RootDomainUserClient",
	};
}

bool IOPMrootDomain::conformsTo(const char* className)
{
	if (std::strcmp(className, "IOPMrootDomain") == 0)
		return true;
	return IOService::conformsTo(className);
}

IOReturn IOPMrootDomain::newUserClient(uint32_t type, IOUserClient** client)
{
	try
	{
		*client = new RootDomainUserClient(this);
		return kIOReturnSuccess;
	}
	catch (const std::exception& e)
	{
		os_log_error(OS_LOG_DEFAULT, "iokitd: cannot open IOPMrootDomain: %s", e.what());
		*client = nullptr;
		return kIOReturnNoResources;
	}
}

bool IOPMrootDomain::acceptsInterest(const char* type)
{
	// General interest messages (clamshell, thermal...) are never sent here,
	// but registering for them is valid.
	return std::strcmp(type, kIOAppPowerStateInterest) == 0
		|| std::strcmp(type, kIOGeneralInterest) == 0;
}

// Like xnu: the change serial number in the upper half, the client in the lower one.
uint32_t IOPMrootDomain::nextNotificationID(uint16_t index)
{
	return (uint32_t(m_serial) << 16) | index;
}

void IOPMrootDomain::tellApps(uint32_t messageType)
{
	uint16_t index = 0;

	m_serial++;
	for (IOInterestNotification* n : interests(kIOAppPowerStateInterest))
	{
		uint32_t id = messageType == kIOMessageSystemWillSleep ? nextNotificationID(++index) : 0;

		if (n->deliver(messageType, id) == KERN_SUCCESS && messageType == kIOMessageSystemWillSleep)
			m_pendingAcks[id] = n;
	}
}

void IOPMrootDomain::systemWillSleep(uint64_t cycle)
{
	os_log(OS_LOG_DEFAULT, "iokitd: host is going to sleep, notifying applications");

	m_cycle = cycle;
	m_sleeping = true;
	m_sleepReadySent = false;
	m_pendingAcks.clear();

	// kIOMessageCanSystemSleep is not sent: logind only announces a suspend
	// that has already been decided, so there is nothing applications could veto.
	tellApps(kIOMessageSystemWillSleep);
	checkSleepReady();
}

void IOPMrootDomain::systemDidWake(uint64_t cycle)
{
	os_log(OS_LOG_DEFAULT, "iokitd: host has woken up, notifying applications");

	m_sleeping = false;
	m_pendingAcks.clear();

	tellApps(kIOMessageSystemWillPowerOn);
	tellApps(kIOMessageSystemHasPoweredOn);
}

void IOPMrootDomain::checkSleepReady()
{
	if (!m_sleeping || m_sleepReadySent || !m_pendingAcks.empty() || !m_logind)
		return;

	m_sleepReadySent = true;
	iokitd_logind_sleep_ready(m_logind, m_cycle);
}

IOReturn IOPMrootDomain::allowPowerChange(uint64_t notificationID)
{
	// Unknown or late IDs are ignored, as on macOS.
	m_pendingAcks.erase(uint32_t(notificationID));
	checkSleepReady();
	return kIOReturnSuccess;
}

IOReturn IOPMrootDomain::cancelPowerChange(uint64_t notificationID)
{
	// Only an idle sleep query (kIOMessageCanSystemSleep) can be cancelled,
	// and those are never sent. Treat it as an acknowledgement, like macOS
	// does for kIOMessageSystemWillSleep.
	return allowPowerChange(notificationID);
}

void IOPMrootDomain::interestRemoved(IOInterestNotification* notification)
{
	for (auto it = m_pendingAcks.begin(); it != m_pendingAcks.end(); )
	{
		if (it->second == notification)
			it = m_pendingAcks.erase(it);
		else
			++it;
	}
	checkSleepReady();
}

// systemd-logind on the host's system bus, through libdbus-1. iokitd shares
// the host's user, so the system bus socket is reachable directly.

static void logindLog(void* context, const char* message)
{
	os_log(OS_LOG_DEFAULT, "iokitd: %{public}s", message);
}

static void logindWillSleep(void* context, uint64_t cycle)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		IOPMrootDomain::instance()->systemWillSleep(cycle);
	});
}

static void logindDidWake(void* context, uint64_t cycle)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		IOPMrootDomain::instance()->systemDidWake(cycle);
	});
}

void IOPMrootDomain::startHostSleepMonitor()
{
	// Connecting and taking the inhibitor are blocking D-Bus calls.
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
		static const struct iokitd_logind_callbacks callbacks = { logindWillSleep, logindDidWake, logindLog };
		struct iokitd_logind* logind = iokitd_logind_start(&callbacks, nullptr, 0);

		if (!logind)
		{
			os_log(OS_LOG_DEFAULT, "iokitd: no host system bus, no sleep/wake notifications");
			return;
		}

		dispatch_async(dispatch_get_main_queue(), ^{
			IOPMrootDomain* domain = IOPMrootDomain::instance();
			domain->m_logind = logind;
			// A suspend may have been announced before we got here.
			domain->checkSleepReady();
		});
	});
}

RootDomainUserClient::RootDomainUserClient(IOPMrootDomain* owner)
: m_owner(owner)
{
}

const char* RootDomainUserClient::className() const
{
	return "RootDomainUserClient";
}

NSDictionary* RootDomainUserClient::matchingDictionary()
{
	return @{};
}

IOExternalMethod* RootDomainUserClient::getExternalMethodForIndex(UInt32 index)
{
	return nullptr;
}

IOReturn RootDomainUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments* arguments)
{
	arguments->scalarOutputCount = 0;
	arguments->structureOutputSize = 0;

	switch (selector)
	{
		case kPMAllowPowerChange:
		case kPMCancelPowerChange:
			if (arguments->scalarInputCount != 1)
				return kIOReturnBadArgument;
			if (selector == kPMAllowPowerChange)
				return m_owner->allowPowerChange(arguments->scalarInput[0]);
			return m_owner->cancelPowerChange(arguments->scalarInput[0]);
		default:
			return kIOReturnUnsupported;
	}
}
