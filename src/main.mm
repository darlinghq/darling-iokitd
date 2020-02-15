#include <unistd.h>
#include <os/log.h>
#include <liblaunch/bootstrap.h>
#include <dispatch/dispatch.h>
#include <dispatch/private.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/pwr_mgt/IOPMLibPrivate.h>
#include <cstdlib>
#include "iokitd.h"
#include "iokitmig.h"
#include "IOObject.h"
#include "IODisplayConnectX11.h"
#include "PowerAssertions.h"

extern "C" {
#include "iokitmigServer.h"
#include "powermanagementServer.h"
}

static const char* SERVICE_NAME = "org.darlinghq.iokitd";
mach_port_t g_masterPort, g_deathPort, g_powerManagementPort;

static void discoverAllDevices();

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
		dispatch_mig_server(portSource, is_iokit_subsystem.maxsize, iokit_server);
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

static void discoverAllDevices()
{
	ServiceRegistry* registry = ServiceRegistry::instance();
	IODisplayConnectX11::discoverDevices(registry);
}
