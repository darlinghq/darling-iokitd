#ifndef _IOKITD_H
#define _IOKITD_H
#include <mach/mach.h>
#include <CoreFoundation/CFString.h>

#define asldebug(...) os_log_debug(OS_LOG_DEFAULT, __VA_ARGS__)

void throwCFStringException(CFStringRef format, ...);
extern mach_port_t g_masterPort, g_deathPort;

#endif

