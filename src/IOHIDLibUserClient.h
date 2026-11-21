/*
 This file is part of Darling.

 Copyright (C) 2026 Lubos Dolezel

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

#ifndef IOKITD_IOHIDLIBUSERCLIENT_H
#define IOKITD_IOHIDLIBUSERCLIENT_H

#include "IOUserClient.h"
#include <cstdint>
#include <map>
#include <set>
#include <vector>

// The user client of an IOHIDDevice, the counterpart of the IOHIDLib plug-in
// that IOHIDDevice (IOKit's hid.subproj) loads. The plug-in asks the service
// for its elements, reads their values, and receives value changes and input
// reports through shared-memory queues (IODataQueue) that it maps with
// IOConnectMapMemory and is woken for by a Mach message.
//
// The messages and structures below are the plug-in's contract with the
// device. They are values of an existing binary interface and are spelled
// out here as they appear to the plug-in.

enum {
	kIOHIDLibConnectTypeManager = 0x00484944, // "HID"
};

// Selectors of the user client
enum {
	kIOHIDLibSelectorDeviceIsValid,
	kIOHIDLibSelectorOpen,
	kIOHIDLibSelectorClose,
	kIOHIDLibSelectorCreateQueue,
	kIOHIDLibSelectorDisposeQueue,
	kIOHIDLibSelectorAddElementToQueue,
	kIOHIDLibSelectorRemoveElementFromQueue,
	kIOHIDLibSelectorQueueHasElement,
	kIOHIDLibSelectorStartQueue,
	kIOHIDLibSelectorStopQueue,
	kIOHIDLibSelectorUpdateElementValues,
	kIOHIDLibSelectorPostElementValues,
	kIOHIDLibSelectorGetReport,
	kIOHIDLibSelectorSetReport,
	kIOHIDLibSelectorGetElementCount,
	kIOHIDLibSelectorGetElements,
	kIOHIDLibSelectorSetQueueAsyncPort,
	kIOHIDLibSelectorReleaseReport,
	kIOHIDLibSelectorCount,
};

// What kind of elements GetElements lists
enum {
	kIOHIDLibElementTypeElement = 0,
	kIOHIDLibElementTypeReportHandler = 1,
};

// An element as the plug-in receives it. A struct with a cookie range
// stands for as many elements as cookies, whose usages run from usageMin
// when usageMin and usageMax differ.
struct IOHIDLibElement
{
	uint32_t cookieMin;
	uint32_t cookieMax;
	uint32_t parentCookie;
	uint32_t type;           // IOHIDElementType
	uint32_t collectionType; // IOHIDElementCollectionType
	uint32_t flags;          // the flags of the HID main item
	uint32_t usagePage;
	uint32_t usageMin;
	uint32_t usageMax;
	int32_t min;
	int32_t max;
	int32_t scaledMin;
	int32_t scaledMax;
	uint32_t size;           // bits of one value
	uint32_t reportSize;
	uint32_t reportCount;
	uint32_t reportID;
	uint32_t unit;
	uint32_t unitExponent;
	uint32_t duplicateValueSize;
	uint32_t duplicateIndex;
	uint32_t bytes;
	uint32_t valueSize;
	uint32_t rawReportCount;
};

static_assert(sizeof(IOHIDLibElement) == 96, "IOHIDElementStruct layout");

// An entry of a queue, and the answer to UpdateElementValues. The value is
// the element's value as 32-bit words, or the bytes of an input report for
// a report handler element.
struct IOHIDLibElementValue
{
	uint32_t cookie;
	uint32_t flagsAndSize;   // flags in the low 8 bits, the total size of the structure above them
	uint32_t timestampLow;
	uint32_t timestampHigh;
	uint32_t generation;
	uint32_t value[1];
};

#define IOHIDLIB_ELEMENT_VALUE_HEADER (sizeof(IOHIDLibElementValue) - sizeof(uint32_t))

// Element types and flags (IOHIDKeys.h)
enum {
	kIOHIDLibTypeInputMisc = 1,
	kIOHIDLibTypeInputButton = 2,
	kIOHIDLibTypeOutput = 129,
	kIOHIDLibTypeFeature = 257,
	kIOHIDLibTypeCollection = 513,
};
enum {
	kIOHIDLibCollectionPhysical = 0,
	kIOHIDLibCollectionApplication = 1,
	kIOHIDLibCollectionLogical = 2,
};
enum {
	kIOHIDLibFlagsVariable = 0x02,
	kIOHIDLibFlagsRelative = 0x04,
	kIOHIDLibFlagsNullState = 0x40,
};

// Report types of IOHIDDeviceSetReport and GetReport
enum {
	kIOHIDLibReportInput = 0,
	kIOHIDLibReportOutput = 1,
	kIOHIDLibReportFeature = 2,
};

// An element of a device with the position of its value in its report
struct IOHIDElementDescription
{
	IOHIDLibElement wire;
	uint32_t bitOffset = 0;  // of the first value, in the report (input or output as the type says; without report ID)
	bool reportHandler = false;
};

class IOHIDLibUserClient;

// What a device has to provide to be served by IOHIDLibUserClient: its
// elements, its current input report, and a call to report changes of that.
class IOHIDReportDevice
{
public:
	virtual ~IOHIDReportDevice();

	// Cookies are 1 and up and unique. The elements that are not the report
	// handler (reportHandler false) come with all of their cookies.
	virtual const std::vector<IOHIDElementDescription>& hidElements() const = 0;
	virtual const std::vector<uint8_t>& hidReport() const = 0;

	// The report ID of the input report, 0 if the device has none. The report
	// of hidReport() is without it; clients receive it with the ID in front.
	virtual uint32_t hidInputReportID() const { return 0; }

	// The output report with that ID as last sent (without the ID), or null
	// if the device has no such output report. Output elements read from it.
	virtual const std::vector<uint8_t>* hidOutputReport(uint32_t reportID) const { return nullptr; }

	// IOHIDDeviceSetReport for an output report (data without the report ID,
	// as long as the report). The device updates what hidOutputReport() returns.
	virtual IOReturn hidSetReport(uint32_t type, uint32_t reportID, const uint8_t* data, size_t length)
	{
		return kIOReturnUnsupported;
	}

	void addHIDClient(IOHIDLibUserClient* client);
	void removeHIDClient(IOHIDLibUserClient* client);

protected:
	// The input report changed from previous to hidReport()
	void hidReportChanged(const std::vector<uint8_t>& previous);

private:
	std::set<IOHIDLibUserClient*> m_hidClients;
};

class IOHIDLibUserClient : public IOUserClient
{
public:
	explicit IOHIDLibUserClient(IOHIDReportDevice* device);
	~IOHIDLibUserClient();

	const char* className() const override;
	NSDictionary* matchingDictionary() override;
	IOExternalMethod* getExternalMethodForIndex(UInt32 index) override;

	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments) override;
	IOReturn asyncExternalMethod(uint32_t selector, IOExternalMethodArguments* arguments,
		mach_port_t wakePort, const uint64_t* reference, uint32_t referenceCount) override;
	IOReturn clientMemory(uint32_t type, int* fd, uint64_t* size) override;

	// Called by the device
	void reportChanged(const std::vector<uint8_t>& previous, const std::vector<uint8_t>& current);
	void deviceGone();

private:
	struct Queue;
	struct ElementRef { size_t description; uint32_t index; };

	const ElementRef* find(uint32_t cookie) const;
	int64_t valueOf(const ElementRef& ref, const std::vector<uint8_t>& report) const;
	Queue* queue(uint64_t token);

	IOReturn getElements(IOExternalMethodArguments* arguments);
	IOReturn updateElementValues(IOExternalMethodArguments* arguments);
	IOReturn createQueue(IOExternalMethodArguments* arguments);
	IOReturn disposeQueue(IOExternalMethodArguments* arguments);
	IOReturn editQueue(uint32_t selector, IOExternalMethodArguments* arguments);
	IOReturn startStopQueue(uint32_t selector, IOExternalMethodArguments* arguments);
	IOReturn getReport(IOExternalMethodArguments* arguments);
	IOReturn setReport(IOExternalMethodArguments* arguments);
	IOReturn postElementValues(IOExternalMethodArguments* arguments);
	const std::vector<uint8_t>& reportOf(const IOHIDElementDescription& e) const;
	std::vector<uint8_t> inputReportWithID() const;

	void enqueue(Queue& queue, uint32_t cookie, uint64_t timestamp, uint32_t generation, const void* bytes, size_t length, uint32_t flags);
	void freeQueue(Queue& queue);

	IOHIDReportDevice* m_device;
	std::vector<ElementRef> m_cookies; // by cookie
	std::vector<uint64_t> m_times;     // when each cookie's value last changed
	std::map<uint64_t, Queue*> m_queues;
	uint64_t m_lastToken = 0;
	uint32_t m_generation = 0;
	bool m_opened = false;
};

#endif
