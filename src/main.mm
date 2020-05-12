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

#include <unistd.h>
#include <os/log.h>
#include <liblaunch/bootstrap.h>
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/pwr_mgt/IOPMLibPrivate.h>
#include <bsm/libbsm.h>
#include <cstdlib>
#include "iokitd.h"
#include "iokitmig.h"
#include "IOObject.h"
#include "IODisplayConnectX11.h"
#include "PowerAssertions.h"
#include "IOSurfaceRoot.h"

extern "C" {
#include "iokitmigServer.h"
#include "powermanagementServer.h"
}

static const char* SERVICE_NAME = "org.darlinghq.iokitd";
mach_port_t g_masterPort, g_deathPort, g_powerManagementPort;

static void discoverAllDevices();
static boolean_t iokitSaveAuditTrail(mach_msg_header_t *message, mach_msg_header_t *reply);

int main(int argc, const char** argv)
{
	mach_port_t bs;
	kern_return_t ret;

	ret = bootstrap_check_in(bootstrap_port, SERVICE_NAME, &g_masterPort);

	if (ret != KERN_SUCCESS)
	{
		os_log_error(OS_LOG_DEFAULT, "%d bootstrap_check_in(%s) failed with error %d", getpid(), SERVICE_NAME, ret);
		return 1;
	}

	ret = bootstrap_check_in(bootstrap_port, kIOPMServerBootstrapName, &g_powerManagementPort);

	if (ret != KERN_SUCCESS)
	{
		os_log_error(OS_LOG_DEFAULT, "%d bootstrap_check_in(%s) failed with error %d", getpid(), SERVICE_NAME, ret);
		return 1;
	}

	ret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_deathPort);
	if (ret != KERN_SUCCESS)
	{
		os_log_error(OS_LOG_DEFAULT, "%d failed to allocate notification port: %d", getpid(), ret);
		return 1;
	}

	// Build IOKit registry here
	discoverAllDevices();
	initPM();

	dispatch_queue_t queue = dispatch_get_main_queue();

	////////////////////////////////
	// IOKit main port            //
	////////////////////////////////
	dispatch_source_t portSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, g_masterPort, 0, queue);

	if (!portSource)
	{
		os_log_error(OS_LOG_DEFAULT, "%d dispatch_source_create() failed for main port", getpid());
		return 1;
	}

	dispatch_source_set_event_handler(portSource, ^{
		dispatch_mig_server(portSource, is_iokit_subsystem.maxsize, iokitSaveAuditTrail);
	});

	////////////////////////////////
	// powerd main port           //
	////////////////////////////////
	dispatch_source_t pwrMgmtPortSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, g_powerManagementPort, 0, queue);

	if (!pwrMgmtPortSource)
	{
		os_log_error(OS_LOG_DEFAULT, "%d dispatch_source_create() failed for pwr mgmt main port", getpid());
		return 1;
	}

	dispatch_source_set_event_handler(pwrMgmtPortSource, ^{
		dispatch_mig_server(pwrMgmtPortSource, _powermanagement_subsystem.maxsize, powermanagement_server); // FIXME
	});

	////////////////////////////////
	// unused port notifications  //
	////////////////////////////////
	dispatch_source_t deathSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, g_deathPort, 0, queue);

	if (!deathSource)
	{
		os_log_error(OS_LOG_DEFAULT, "%d dispatch_source_create() failed for notification port", getpid());
		return 1;
	}

	dispatch_source_set_event_handler(deathSource, ^{
		dispatch_mig_server(deathSource, sizeof(mach_no_senders_notification_t), IOObject::deathNotify);
	});

	dispatch_resume(portSource);
	dispatch_resume(deathSource);
	dispatch_resume(pwrMgmtPortSource);

	os_log(OS_LOG_DEFAULT, "iokitd up and running.");

	dispatch_main();
	return 0;
}

static boolean_t iokitSaveAuditTrail(mach_msg_header_t *message, mach_msg_header_t *reply)
{
	const mach_msg_context_trailer_t* trailer = reinterpret_cast<mach_msg_context_trailer_t*>(reinterpret_cast<char*>(message) + message->msgh_size);
	audit_token_to_au32(trailer->msgh_audit, nullptr, nullptr, nullptr, nullptr, nullptr, &g_iokitCurrentCallerPID, nullptr, nullptr);
	return iokit_server(message, reply);
}

static void discoverAllDevices()
{
	ServiceRegistry* registry = ServiceRegistry::instance();
	// Trick to make sure the root object gets instantiated first and gets the lowest ID
	IORegistryEntry::root();
	IODisplayConnectX11::discoverDevices(registry);
	IOSurfaceRoot::registerSelf(registry);
}
