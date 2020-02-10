#include "iokitd.h"
#include <stdexcept>
#include <stdarg.h>
#include <string>

void throwCFStringException(CFStringRef format, ...)
{
	CFStringRef text;

	va_list ap;
	va_start(ap, format);
	
	text = CFStringCreateWithFormatAndArguments(nullptr, nullptr, format, ap);

	va_end(ap);

	std::string str = CFStringGetCStringPtr(text, kCFStringEncodingASCII);
	CFRelease(text);

	throw std::runtime_error(str);
}
