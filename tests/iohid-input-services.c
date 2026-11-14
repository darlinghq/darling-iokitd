/*
 Copyright (C) 2026 Lubos Dolezel

 Guest test: host keyboards and mice are IOHIDDevice services of the IOKit
 registry, with the documented HID properties.

   darling shell ./iohid-input-services            (generic)
   darling shell ./iohid-input-services --virtual  (with uinput-input.py running on the host)

 Without permission to read /dev/input no keyboard or mouse is published; the
 generic run then prints SKIP and exits 0. With --virtual the devices that
 uinput-input.py creates (vendor 0x1234, product 0x5678, products "Darling
 Test Keyboard" and "Darling Test Mouse") must be found.

 For every IOHIDDevice service whose PrimaryUsagePage is 1 and PrimaryUsage
 is 6 (Keyboard) or 2 (Mouse): Transport is a string, VendorID and ProductID
 are numbers, DeviceUsagePairs lists the primary usage, MaxInputReportSize is
 positive, and ReportDescriptor is a data blob that starts with
 "Usage Page (Generic Desktop), Usage (primary), Collection (Application)"
 and ends with End Collection.
*/
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static int failed;

static void check(int ok, const char* what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failed = 1;
	fflush(stdout);
}

static int number(CFTypeRef v)
{
	int n = -1;
	if (v && CFGetTypeID(v) == CFNumberGetTypeID())
		CFNumberGetValue(v, kCFNumberIntType, &n);
	return n;
}

int main(int argc, char** argv)
{
	int virtualRun = argc > 1 && !strcmp(argv[1], "--virtual");
	int keyboards = 0, mice = 0, foundVirtualKeyboard = 0, foundVirtualMouse = 0;
	io_iterator_t it;
	io_service_t service;

	kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOHIDDevice"), &it);
	check(kr == KERN_SUCCESS, "matching services of class IOHIDDevice");
	if (kr != KERN_SUCCESS)
		return 1;

	while ((service = IOIteratorNext(it)))
	{
		CFTypeRef page = IORegistryEntryCreateCFProperty(service, CFSTR("PrimaryUsagePage"), NULL, 0);
		CFTypeRef usageValue = IORegistryEntryCreateCFProperty(service, CFSTR("PrimaryUsage"), NULL, 0);
		int usage = number(usageValue);

		if (number(page) == 1 && (usage == 6 || usage == 2))
		{
			CFTypeRef transport = IORegistryEntryCreateCFProperty(service, CFSTR("Transport"), NULL, 0);
			CFTypeRef vendor = IORegistryEntryCreateCFProperty(service, CFSTR("VendorID"), NULL, 0);
			CFTypeRef product = IORegistryEntryCreateCFProperty(service, CFSTR("ProductID"), NULL, 0);
			CFTypeRef name = IORegistryEntryCreateCFProperty(service, CFSTR("Product"), NULL, 0);
			CFTypeRef pairs = IORegistryEntryCreateCFProperty(service, CFSTR("DeviceUsagePairs"), NULL, 0);
			CFTypeRef maxInput = IORegistryEntryCreateCFProperty(service, CFSTR("MaxInputReportSize"), NULL, 0);
			CFTypeRef descriptor = IORegistryEntryCreateCFProperty(service, CFSTR("ReportDescriptor"), NULL, 0);
			char label[128] = "device";

			if (usage == 6) keyboards++; else mice++;
			if (name && CFGetTypeID(name) == CFStringGetTypeID())
				CFStringGetCString(name, label, sizeof(label), kCFStringEncodingUTF8);
			printf("-- %s (%s)\n", label, usage == 6 ? "keyboard" : "mouse");

			check(IOObjectConformsTo(service, "IOHIDDevice"), "conforms to IOHIDDevice");
			check(transport && CFGetTypeID(transport) == CFStringGetTypeID(), "Transport is a string");
			check(number(vendor) >= 0 && number(product) >= 0, "VendorID and ProductID are numbers");
			check(name && CFGetTypeID(name) == CFStringGetTypeID(), "Product is a string");
			check(maxInput && number(maxInput) > 0, "MaxInputReportSize is positive");

			int pairOk = 0;
			if (pairs && CFGetTypeID(pairs) == CFArrayGetTypeID() && CFArrayGetCount(pairs) >= 1)
			{
				CFDictionaryRef pair = CFArrayGetValueAtIndex(pairs, 0);
				pairOk = CFGetTypeID(pair) == CFDictionaryGetTypeID()
					&& number(CFDictionaryGetValue(pair, CFSTR("DeviceUsagePage"))) == 1
					&& number(CFDictionaryGetValue(pair, CFSTR("DeviceUsage"))) == usage;
			}
			check(pairOk, "DeviceUsagePairs lists the primary usage");

			int descriptorOk = 0;
			if (descriptor && CFGetTypeID(descriptor) == CFDataGetTypeID() && CFDataGetLength(descriptor) > 8)
			{
				const UInt8* b = CFDataGetBytePtr(descriptor);
				CFIndex n = CFDataGetLength(descriptor);
				descriptorOk = b[0] == 0x05 && b[1] == 0x01 && b[2] == 0x09 && b[3] == usage
					&& b[4] == 0xa1 && b[5] == 0x01 && b[n - 1] == 0xc0;
			}
			check(descriptorOk, "ReportDescriptor is a Generic Desktop application collection");

			if (virtualRun && name && number(vendor) == 0x1234 && number(product) == 0x5678)
			{
				if (!strcmp(label, "Darling Test Keyboard") && usage == 6)
					foundVirtualKeyboard = 1;
				if (!strcmp(label, "Darling Test Mouse") && usage == 2)
					foundVirtualMouse = 1;
			}
			if (transport) CFRelease(transport);
			if (vendor) CFRelease(vendor);
			if (product) CFRelease(product);
			if (name) CFRelease(name);
			if (pairs) CFRelease(pairs);
			if (maxInput) CFRelease(maxInput);
			if (descriptor) CFRelease(descriptor);
		}
		if (page) CFRelease(page);
		if (usageValue) CFRelease(usageValue);
		IOObjectRelease(service);
	}
	IOObjectRelease(it);

	if (virtualRun)
	{
		check(foundVirtualKeyboard, "virtual keyboard published");
		check(foundVirtualMouse, "virtual mouse published");
	}
	else if (keyboards + mice == 0)
	{
		printf("SKIP: no host keyboard or mouse is readable\n");
		return failed;
	}

	printf("%s: %d keyboards, %d mice\n", failed ? "FAIL" : "PASS", keyboards, mice);
	return failed;
}
