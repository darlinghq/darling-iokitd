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

#ifndef IOKITD_IOPMROOTDOMAIN_H
#define IOKITD_IOPMROOTDOMAIN_H

#include "IOUserClient.h"
#include "ServiceRegistry.h"
#include <map>

struct iokitd_logind;

// The power management root (IOPower:/IOPowerConnection/IOPMrootDomain).
// IORegisterForSystemPower() opens it and registers for
// kIOAppPowerStateInterest; host suspend and resume, as announced by
// systemd-logind, are passed on as kIOMessageSystemWillSleep (which clients
// acknowledge with IOAllowPowerChange()), kIOMessageSystemWillPowerOn and
// kIOMessageSystemHasPoweredOn.
class IOPMrootDomain : public IOService
{
public:
	static IOPMrootDomain* instance();
	static void registerSelf(ServiceRegistry* targetServiceRegistry);

	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	NSDictionary* getProperties() override;
	bool conformsTo(const char* className) override;
	IOReturn newUserClient(uint32_t type, IOUserClient** client) override;
	bool acceptsInterest(const char* type) override;

	// IOAllowPowerChange() / IOCancelPowerChange()
	IOReturn allowPowerChange(uint64_t notificationID);
	IOReturn cancelPowerChange(uint64_t notificationID);

	// Follows host sleep through logind, without blocking the caller.
	void startHostSleepMonitor();

	// Main queue
	void systemWillSleep(uint64_t cycle);
	void systemDidWake(uint64_t cycle);
protected:
	void interestRemoved(IOInterestNotification* notification) override;
private:
	IOPMrootDomain() = default;
	uint32_t nextNotificationID(uint16_t index);
	void tellApps(uint32_t messageType);
	void checkSleepReady();

	struct iokitd_logind* m_logind = nullptr;
	bool m_sleeping = false;
	bool m_sleepReadySent = false;
	uint64_t m_cycle = 0;
	uint16_t m_serial = 0;
	// Outstanding kIOMessageSystemWillSleep acknowledgements by notification ID
	std::map<uint32_t, IOInterestNotification*> m_pendingAcks;
};

// The connection returned by IOServiceOpen() on the root domain
class RootDomainUserClient : public IOUserClient
{
public:
	RootDomainUserClient(IOPMrootDomain* owner);

	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	IOExternalMethod* getExternalMethodForIndex(UInt32 index) override;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments) override;
private:
	IOPMrootDomain* m_owner;
};

#endif
