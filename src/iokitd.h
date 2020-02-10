#ifndef _IOKITD_H
#define _IOKITD_H
#include <mach/mach.h>

extern mach_port_t g_machServerPort;

#define asldebug(...) os_log_debug(OS_LOG_DEFAULT, __VA_ARGS__)

#endif

