#include <unistd.h>
#include <os/log.h>
#include <liblaunch/launch.h>
#include <dispatch/dispatch.h>
#include <cstdlib>
#include "iokitd.h"

static const char* SERVICE_NAME = "org.darlinghq.iokitd";
mach_port_t g_machServerPort;

static void launch_config();

int main(int argc, const char** argv)
{
	launch_config();

	dispatch_main();
	return 0;
}

static void launch_config()
{
	launch_data_t tmp, pdict;
	kern_return_t status;
	launch_data_t launch_dict;

	tmp = launch_data_new_string(LAUNCH_KEY_CHECKIN);
	launch_dict = launch_msg(tmp);
	launch_data_free(tmp);

	if (!launch_dict)
	{
		os_log_error(OS_LOG_DEFAULT, "%d launchd checkin failed\n", global.pid);
		exit(1);
	}

	tmp = launch_data_dict_lookup(launch_dict, LAUNCH_JOBKEY_MACHSERVICES);
	if (!tmp)
	{
		os_log_error(OS_LOG_DEFAULT, "%d launchd lookup of LAUNCH_JOBKEY_MACHSERVICES failed\n", getpid());
		exit(1);
	}

	pdict = launch_data_dict_lookup(tmp, SERVICE_NAME);
	if (!pdict)
	{
		os_log_error(OS_LOG_DEFAULT, "%d launchd lookup of SERVICE_NAME failed\n", getpid());
		exit(1);
	}

	g_machServerPort = launch_data_get_machport(pdict);
}


