#include <unistd.h>
#include <os/log.h>
#include <liblaunch/bootstrap.h>
#include <dispatch/dispatch.h>
#include <cstdlib>
#include "iokitd.h"
#include "iokitmig.h"

extern "C" {
#include "iokitmigServer.h"
}

static const char* SERVICE_NAME = "org.darlinghq.iokitd";
static void service_mach_message(mach_port_t serverPort);

int main(int argc, const char** argv)
{
	mach_port_t bs, mp;
	kern_return_t ret;

	task_get_bootstrap_port(mach_task_self(), &bs);
	ret = bootstrap_check_in(bs, SERVICE_NAME, &mp);

	mach_port_destroy(mach_task_self(), bs);

	if (ret != KERN_SUCCESS)
	{
		os_log_error(OS_LOG_DEFAULT, "%d bootstrap_check_in() failed with error %d", getpid(), ret);
		return 1;
	}

	dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
	dispatch_source_t portSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, mp, 0, queue);

	if (!portSource)
	{
		os_log_error(OS_LOG_DEFAULT, "%d dispatch_source_create() failed", getpid());
		return 1;
	}

	dispatch_source_set_event_handler(portSource, ^{
		service_mach_message(mp);
	});
	dispatch_resume(portSource);

	dispatch_main();
	return 0;
}

typedef union
{
	mach_msg_header_t head;
	union __RequestUnion__iokit_subsystem request;
} iokit_request_msg;

typedef union
{
	mach_msg_header_t head;
	union __ReplyUnion__iokit_subsystem reply;
} iokit_reply_msg;

static void service_mach_message(mach_port_t serverPort)
{
	__block kern_return_t status;
	uint32_t rbits, sbits;
	iokit_request_msg *request;
	iokit_reply_msg *reply;
	char rbuf[sizeof(iokit_request_msg) + MAX_TRAILER_SIZE];
	char sbuf[sizeof(iokit_reply_msg) + MAX_TRAILER_SIZE];

	while (true)
	{
		memset(rbuf, 0, sizeof(rbuf));
		memset(sbuf, 0, sizeof(sbuf));

		request = (iokit_request_msg *)rbuf;
		reply = (iokit_reply_msg *)sbuf;

		request->head.msgh_local_port = serverPort;
		request->head.msgh_size = sizeof(iokit_request_msg) + MAX_TRAILER_SIZE;

		rbits = MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT) | MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | MACH_RCV_VOUCHER;
		sbits = MACH_SEND_MSG;

		status = mach_msg(&(request->head), rbits, 0, request->head.msgh_size, serverPort, 0, MACH_PORT_NULL);
		if (status != KERN_SUCCESS) return;

		voucher_mach_msg_state_t voucher = voucher_mach_msg_adopt(&(request->head));

		status = iokit_server(&(request->head), &(reply->head));

		if (!status && (request->head.msgh_bits & MACH_MSGH_BITS_COMPLEX))
		{
			/* destroy the request - but not the reply port */
			request->head.msgh_remote_port = MACH_PORT_NULL;
			mach_msg_destroy(&(request->head));
		}

		if (reply->head.msgh_remote_port)
		{
			status = mach_msg(&(reply->head), sbits, reply->head.msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
			if (status == MACH_SEND_INVALID_DEST || status == MACH_SEND_TIMED_OUT)
			{
				/* deallocate reply port rights not consumed by failed mach_msg() send */
				mach_msg_destroy(&(reply->head));
			}
		}

		voucher_mach_msg_revert(voucher);
	}
}
