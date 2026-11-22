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

#include "IOHIDLibUserClient.h"
#include "IOAsyncCompletion.h"
#import <Foundation/NSDictionary.h>
#include <IOKit/IODataQueueClient.h>
#include <IOKit/IODataQueueShared.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <os/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

namespace {

// Entries of queues that were created without a depth
const uint32_t kDefaultQueueDepth = 1024;
// What one entry needs at most, besides the report of a report handler: the
// entry header, the value header and a few words of value
const uint32_t kEntryOverhead = 64;

uint32_t alignWord(uint32_t size)
{
	return (size + 3) & ~3u;
}

uint64_t now()
{
	return mach_absolute_time();
}

}

struct IOHIDLibUserClient::Queue
{
	uint64_t token = 0;
	std::set<uint32_t> cookies;
	bool started = false;
	int fd = -1;
	IODataQueueMemory* memory = nullptr;
	size_t memorySize = 0;
	mach_port_t port = MACH_PORT_NULL;
};

IOHIDReportDevice::~IOHIDReportDevice()
{
	// Clients that outlive the device stop serving it
	while (!m_hidClients.empty())
	{
		IOHIDLibUserClient* client = *m_hidClients.begin();
		m_hidClients.erase(m_hidClients.begin());
		client->deviceGone();
	}
}

void IOHIDReportDevice::addHIDClient(IOHIDLibUserClient* client)
{
	m_hidClients.insert(client);
}

void IOHIDReportDevice::removeHIDClient(IOHIDLibUserClient* client)
{
	m_hidClients.erase(client);
}

void IOHIDReportDevice::hidReportChanged(const std::vector<uint8_t>& previous)
{
	hidReportChanged(hidInputReportID(), previous, hidReport());
}

void IOHIDReportDevice::hidReportChanged(uint32_t reportID, const std::vector<uint8_t>& previous, const std::vector<uint8_t>& current)
{
	// A client cannot go away while it is told
	std::vector<IOHIDLibUserClient*> clients(m_hidClients.begin(), m_hidClients.end());
	for (IOHIDLibUserClient* client : clients)
		client->reportChanged(reportID, previous, current);
}

IOHIDLibUserClient::IOHIDLibUserClient(IOHIDReportDevice* device)
: m_device(device)
{
	const auto& elements = device->hidElements();
	uint32_t maxCookie = 0;

	for (const IOHIDElementDescription& e : elements)
		maxCookie = std::max(maxCookie, e.wire.cookieMax);

	m_cookies.assign(maxCookie + 1, ElementRef{ size_t(-1), 0 });
	for (size_t i = 0; i < elements.size(); i++)
	{
		const IOHIDLibElement& wire = elements[i].wire;
		for (uint32_t cookie = wire.cookieMin; cookie <= wire.cookieMax; cookie++)
			m_cookies[cookie] = ElementRef{ i, cookie - wire.cookieMin };
	}
	m_times.assign(maxCookie + 1, now());
	device->addHIDClient(this);
}

IOHIDLibUserClient::~IOHIDLibUserClient()
{
	if (m_device)
		m_device->removeHIDClient(this);
	for (auto& [token, queue] : m_queues)
	{
		freeQueue(*queue);
		delete queue;
	}
}

const char* IOHIDLibUserClient::className() const
{
	return "IOHIDLibUserClient";
}

NSDictionary* IOHIDLibUserClient::matchingDictionary()
{
	return @{};
}

IOExternalMethod* IOHIDLibUserClient::getExternalMethodForIndex(UInt32 index)
{
	// Everything goes through externalMethod()
	return nullptr;
}

const IOHIDLibUserClient::ElementRef* IOHIDLibUserClient::find(uint32_t cookie) const
{
	if (cookie >= m_cookies.size() || m_cookies[cookie].description == size_t(-1))
		return nullptr;
	return &m_cookies[cookie];
}

int64_t IOHIDLibUserClient::valueOf(const ElementRef& ref, const std::vector<uint8_t>& report) const
{
	const IOHIDElementDescription& e = m_device->hidElements()[ref.description];

	if (e.arrayUsages)
	{
		// 1 while a slot of the array holds the usage of the element
		uint32_t usage = e.wire.usageMin + ref.index;

		for (uint32_t slot = 0; slot < e.slotCount; slot++)
		{
			uint64_t held = 0;
			for (uint32_t i = 0; i < e.slotSize && i < 32; i++)
			{
				uint32_t bit = e.slotBitOffset + slot * e.slotSize + i;
				if (bit / 8 < report.size() && (report[bit / 8] >> (bit % 8)) & 1)
					held |= uint64_t(1) << i;
			}
			if (held == usage)
				return 1;
		}
		return 0;
	}

	// The first of fields that share a usage is all of them together
	bool whole = e.wire.duplicateValueSize && ref.index == 0;
	uint32_t bits = std::min<uint32_t>(whole ? e.wire.size : e.wire.reportSize, 32);
	uint32_t first = e.bitOffset + (whole ? 0 : ref.index) * e.wire.reportSize;
	uint64_t value = 0;

	for (uint32_t i = 0; i < bits; i++)
	{
		uint32_t bit = first + i;
		if (bit / 8 < report.size() && (report[bit / 8] >> (bit % 8)) & 1)
			value |= uint64_t(1) << i;
	}
	if (e.wire.min < 0 && bits < 64 && (value >> (bits - 1)) & 1)
		value |= ~uint64_t(0) << bits;
	return (int64_t) value;
}

IOHIDLibUserClient::Queue* IOHIDLibUserClient::queue(uint64_t token)
{
	auto it = m_queues.find(token);
	return it == m_queues.end() ? nullptr : it->second;
}

void IOHIDLibUserClient::deviceGone()
{
	m_device = nullptr;
	for (auto& [token, queue] : m_queues)
		queue->started = false;
}

IOReturn IOHIDLibUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments* args)
{
	if (!m_device)
		return kIOReturnNoDevice;

	switch (selector)
	{
		case kIOHIDLibSelectorDeviceIsValid:
			if (args->scalarOutputCount >= 1)
				args->scalarOutput[0] = 1;
			if (args->scalarOutputCount >= 2)
				args->scalarOutput[1] = 0; // generation
			return kIOReturnSuccess;
		case kIOHIDLibSelectorOpen:
			m_opened = true;
			return kIOReturnSuccess;
		case kIOHIDLibSelectorClose:
			m_opened = false;
			for (auto& [token, q] : m_queues)
				q->started = false;
			return kIOReturnSuccess;
		case kIOHIDLibSelectorGetElementCount:
		{
			uint64_t elements = 0, handlers = 0;
			for (const IOHIDElementDescription& e : m_device->hidElements())
				(e.reportHandler ? handlers : elements)++;
			if (args->scalarOutputCount < 2)
				return kIOReturnBadArgument;
			args->scalarOutput[0] = elements;
			args->scalarOutput[1] = handlers;
			return kIOReturnSuccess;
		}
		case kIOHIDLibSelectorGetElements:
			return getElements(args);
		case kIOHIDLibSelectorUpdateElementValues:
			return updateElementValues(args);
		case kIOHIDLibSelectorCreateQueue:
			return createQueue(args);
		case kIOHIDLibSelectorDisposeQueue:
			return disposeQueue(args);
		case kIOHIDLibSelectorAddElementToQueue:
		case kIOHIDLibSelectorRemoveElementFromQueue:
		case kIOHIDLibSelectorQueueHasElement:
			return editQueue(selector, args);
		case kIOHIDLibSelectorStartQueue:
		case kIOHIDLibSelectorStopQueue:
			return startStopQueue(selector, args);
		case kIOHIDLibSelectorGetReport:
			return getReport(args);
		case kIOHIDLibSelectorReleaseReport:
			return kIOReturnSuccess;
		case kIOHIDLibSelectorPostElementValues:
			return postElementValues(args);
		case kIOHIDLibSelectorSetReport:
			return setReport(args);
	}
	return kIOReturnBadArgument;
}

IOReturn IOHIDLibUserClient::asyncExternalMethod(uint32_t selector, IOExternalMethodArguments* args,
	mach_port_t wakePort, const uint64_t* reference, uint32_t referenceCount)
{
	if (selector == kIOHIDLibSelectorSetReport || selector == kIOHIDLibSelectorGetReport)
	{
		// The report is exchanged before this returns, so the reply carries the data and the
		// result of a call that fails. A call that succeeds also completes: the completion
		// message goes to the wake port, once, with the result and the length of the report
		// (what the callback of IOHIDDeviceSetReportWithCallback and IOHIDDeviceGetReportWithCallback
		// gets). The operation never takes long, so a timeout of the call cannot expire first.
		bool set = selector == kIOHIDLibSelectorSetReport;
		uint64_t length = set ? args->structureInputSize : 0;
		IOReturn result = externalMethod(selector, args);

		if (result == kIOReturnSuccess)
		{
			if (!set)
				length = args->structureOutputSize;
			iokitSendAsyncCompletion(wakePort, reference, referenceCount, kIOReturnSuccess, &length, 1);
		}
		if (MACH_PORT_VALID(wakePort))
			mach_port_deallocate(mach_task_self(), wakePort);
		return result;
	}
	if (selector != kIOHIDLibSelectorSetQueueAsyncPort)
		return IOUserClient::asyncExternalMethod(selector, args, wakePort, reference, referenceCount);

	// The wake port is the one the queue's notification goes to
	Queue* q = args->scalarInputCount >= 1 ? queue(args->scalarInput[0]) : nullptr;

	if (!q || !MACH_PORT_VALID(wakePort))
	{
		if (MACH_PORT_VALID(wakePort))
			mach_port_deallocate(mach_task_self(), wakePort);
		return kIOReturnBadArgument;
	}
	if (MACH_PORT_VALID(q->port))
		mach_port_deallocate(mach_task_self(), q->port);
	q->port = wakePort;
	IODataQueueSetNotificationPort(q->memory, wakePort);
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::getElements(IOExternalMethodArguments* args)
{
	if (args->scalarInputCount < 1)
		return kIOReturnBadArgument;

	bool reportHandlers = args->scalarInput[0] == kIOHIDLibElementTypeReportHandler;
	uint8_t* out = static_cast<uint8_t*>(args->structureOutput);
	size_t used = 0;

	for (const IOHIDElementDescription& e : m_device->hidElements())
	{
		if (e.reportHandler != reportHandlers)
			continue;
		if (used + sizeof(e.wire) > args->structureOutputSize)
			return kIOReturnNoSpace;
		std::memcpy(out + used, &e.wire, sizeof(e.wire));
		used += sizeof(e.wire);
	}
	args->structureOutputSize = used;
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::updateElementValues(IOExternalMethodArguments* args)
{
	const uint32_t* cookies = static_cast<const uint32_t*>(args->structureInput);
	size_t count = args->structureInputSize / sizeof(uint32_t);
	uint8_t* out = static_cast<uint8_t*>(args->structureOutput);
	size_t used = 0;

	for (size_t i = 0; i < count; i++)
	{
		const ElementRef* ref = find(cookies[i]);
		if (!ref)
			return kIOReturnBadArgument;

		const IOHIDElementDescription& e = m_device->hidElements()[ref->description];
		std::vector<uint8_t> handlerBytes = e.reportHandler ? inputReportWithID(e.wire.reportID) : std::vector<uint8_t>();
		bool blob = isBlob(e, *ref);
		uint32_t length = e.reportHandler ? alignWord(handlerBytes.size()) : blob ? alignWord(e.wire.valueSize) : 4;
		size_t total = IOHIDLIB_ELEMENT_VALUE_HEADER + length;

		if (e.wire.type == kIOHIDLibTypeCollection)
			return kIOReturnBadArgument;
		if (used + total > args->structureOutputSize)
			return kIOReturnNoSpace;

		IOHIDLibElementValue* value = reinterpret_cast<IOHIDLibElementValue*>(out + used);
		uint64_t stamp = m_times[cookies[i]];

		std::memset(value, 0, total);
		value->cookie = cookies[i];
		value->flagsAndSize = uint32_t(total) << 8;
		value->timestampLow = uint32_t(stamp);
		value->timestampHigh = uint32_t(stamp >> 32);
		value->generation = m_generation;
		if (e.reportHandler)
			std::memcpy(value->value, handlerBytes.data(), handlerBytes.size());
		else if (blob)
		{
			// A long value (more than 32 bits): the bytes of the report, as is
			const std::vector<uint8_t>& source = reportOf(e);
			size_t first = e.bitOffset / 8;
			for (size_t b = 0; b < e.wire.valueSize && first + b < source.size(); b++)
				reinterpret_cast<uint8_t*>(value->value)[b] = source[first + b];
		}
		else
			value->value[0] = uint32_t(valueOf(*ref, reportOf(e)));
		used += total;
	}
	args->structureOutputSize = used;
	return kIOReturnSuccess;
}

bool IOHIDLibUserClient::isBlob(const IOHIDElementDescription& e, const ElementRef& ref) const
{
	// More than 32 bits: the bytes of the report as they are (not for the fields after the first of
	// fields that share a usage, which are one field each)
	return e.wire.size > 32 && !e.reportHandler && !e.arrayUsages && !(e.wire.duplicateValueSize && ref.index != 0);
}

const std::vector<uint8_t>& IOHIDLibUserClient::reportOf(const IOHIDElementDescription& e) const
{
	static const std::vector<uint8_t> none;

	if (e.wire.type == kIOHIDLibTypeOutput)
	{
		const std::vector<uint8_t>* report = m_device->hidOutputReport(e.wire.reportID);
		return report ? *report : none;
	}
	if (e.wire.type == kIOHIDLibTypeFeature)
		return none;
	const std::vector<uint8_t>* report = m_device->hidInputReport(e.wire.reportID);
	return report ? *report : none;
}

std::vector<uint8_t> IOHIDLibUserClient::inputReportWithID(uint32_t id) const
{
	std::vector<uint8_t> bytes;
	const std::vector<uint8_t>* report = m_device->hidInputReport(id);

	if (!report)
		return bytes;
	if (id)
		bytes.push_back(uint8_t(id));
	bytes.insert(bytes.end(), report->begin(), report->end());
	return bytes;
}

IOReturn IOHIDLibUserClient::getReport(IOExternalMethodArguments* args)
{
	// Input: the controller's or keyboard's current state. Output: what was
	// last sent. A report that has a report ID comes with it in front.
	if (args->scalarInputCount < 2)
		return kIOReturnBadArgument;

	uint32_t type = uint32_t(args->scalarInput[0]), id = uint32_t(args->scalarInput[1]);
	std::vector<uint8_t> bytes;

	if (type == kIOHIDLibReportInput && m_device->hidInputReport(id))
		bytes = inputReportWithID(id);
	else if (type == kIOHIDLibReportOutput && m_device->hidOutputReport(id))
	{
		if (id)
			bytes.push_back(uint8_t(id));
		bytes.insert(bytes.end(), m_device->hidOutputReport(id)->begin(), m_device->hidOutputReport(id)->end());
	}
	else
		return kIOReturnUnsupported;

	if (args->structureOutputSize < bytes.size())
		return kIOReturnNoSpace;
	std::memcpy(args->structureOutput, bytes.data(), bytes.size());
	args->structureOutputSize = bytes.size();
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::setReport(IOExternalMethodArguments* args)
{
	if (args->scalarInputCount < 2)
		return kIOReturnBadArgument;

	uint32_t type = uint32_t(args->scalarInput[0]), id = uint32_t(args->scalarInput[1]);
	const uint8_t* data = static_cast<const uint8_t*>(args->structureInput);
	size_t length = args->structureInputSize;

	if (type != kIOHIDLibReportOutput && type != kIOHIDLibReportFeature)
		return kIOReturnBadArgument;
	// A report with an ID comes with it as its first byte
	if (id)
	{
		if (length < 1 || data[0] != id)
			return kIOReturnBadArgument;
		data++;
		length--;
	}
	return m_device->hidSetReport(type, id, data, length);
}

IOReturn IOHIDLibUserClient::postElementValues(IOExternalMethodArguments* args)
{
	// One element value: the cookie, the length in bytes and the value as 32-bit words
	if (args->structureInputSize < 2 * sizeof(uint32_t))
		return kIOReturnBadArgument;

	const uint32_t* input = static_cast<const uint32_t*>(args->structureInput);
	const ElementRef* ref = find(input[0]);
	uint32_t length = input[1];

	if (!ref || length > args->structureInputSize - 2 * sizeof(uint32_t))
		return kIOReturnBadArgument;

	const IOHIDElementDescription& e = m_device->hidElements()[ref->description];
	const std::vector<uint8_t>* current = e.wire.type == kIOHIDLibTypeOutput ? m_device->hidOutputReport(e.wire.reportID) : nullptr;

	if (e.wire.type != kIOHIDLibTypeOutput && e.wire.type != kIOHIDLibTypeFeature)
		return kIOReturnBadArgument;
	if (!current)
		return kIOReturnUnsupported;

	std::vector<uint8_t> report = *current;
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input + 2);

	if (e.wire.size > 32)
	{
		size_t first = e.bitOffset / 8;
		for (size_t b = 0; b < length && b < e.wire.valueSize && first + b < report.size(); b++)
			report[first + b] = bytes[b];
	}
	else
	{
		uint32_t word = 0;
		std::memcpy(&word, bytes, std::min<uint32_t>(length, 4));
		uint32_t first = e.bitOffset + ref->index * e.wire.reportSize;
		for (uint32_t i = 0; i < e.wire.size; i++)
		{
			uint32_t bit = first + i;
			if (bit / 8 >= report.size())
				break;
			if ((word >> i) & 1)
				report[bit / 8] |= uint8_t(1u << (bit % 8));
			else
				report[bit / 8] &= uint8_t(~(1u << (bit % 8)));
		}
	}
	return m_device->hidSetReport(e.wire.type == kIOHIDLibTypeOutput ? kIOHIDLibReportOutput : kIOHIDLibReportFeature,
		e.wire.reportID, report.data(), report.size());
}

IOReturn IOHIDLibUserClient::createQueue(IOExternalMethodArguments* args)
{
	if (args->scalarInputCount < 2 || args->scalarOutputCount < 1)
		return kIOReturnBadArgument;

	uint32_t depth = uint32_t(args->scalarInput[1]) ? uint32_t(args->scalarInput[1]) : kDefaultQueueDepth;
	uint32_t entry = kEntryOverhead + alignWord(m_device->hidMaxInputReportSize() + 1);
	size_t queueSize = size_t(depth) * entry;
	size_t total = DATA_QUEUE_MEMORY_HEADER_SIZE + queueSize + DATA_QUEUE_MEMORY_APPENDIX_SIZE;
	std::unique_ptr<Queue> q(new Queue);
	char name[64];

	// Memory that the client can map: an anonymous shared memory file
	q->token = ++m_lastToken;
	snprintf(name, sizeof name, "/iokitd-hid-%d-%llu", getpid(), (unsigned long long) q->token);
	q->fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (q->fd < 0)
		return kIOReturnNoResources;
	shm_unlink(name);
	if (ftruncate(q->fd, total) != 0)
	{
		close(q->fd);
		return kIOReturnNoMemory;
	}
	void* memory = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, q->fd, 0);
	if (memory == MAP_FAILED)
	{
		close(q->fd);
		return kIOReturnNoMemory;
	}
	q->memory = static_cast<IODataQueueMemory*>(memory);
	q->memorySize = total;
	std::memset(memory, 0, total);
	q->memory->queueSize = queueSize;

	uint64_t token = q->token;

	args->scalarOutput[0] = token;
	m_queues[token] = q.release();
	return kIOReturnSuccess;
}

void IOHIDLibUserClient::freeQueue(Queue& q)
{
	if (q.memory)
		munmap(q.memory, q.memorySize);
	if (q.fd >= 0)
		close(q.fd);
	if (MACH_PORT_VALID(q.port))
		mach_port_deallocate(mach_task_self(), q.port);
	q.memory = nullptr;
	q.fd = -1;
	q.port = MACH_PORT_NULL;
}

IOReturn IOHIDLibUserClient::disposeQueue(IOExternalMethodArguments* args)
{
	auto it = args->scalarInputCount >= 1 ? m_queues.find(args->scalarInput[0]) : m_queues.end();

	if (it == m_queues.end())
		return kIOReturnBadArgument;
	freeQueue(*it->second);
	delete it->second;
	m_queues.erase(it);
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::editQueue(uint32_t selector, IOExternalMethodArguments* args)
{
	if (args->scalarInputCount < 2 || args->scalarOutputCount < 1)
		return kIOReturnBadArgument;

	Queue* q = queue(args->scalarInput[0]);
	uint32_t cookie = uint32_t(args->scalarInput[1]);

	if (!q || !find(cookie))
		return kIOReturnBadArgument;

	// The memory of a queue is the same whatever it holds
	args->scalarOutput[0] = 0;
	switch (selector)
	{
		case kIOHIDLibSelectorAddElementToQueue:
			q->cookies.insert(cookie);
			break;
		case kIOHIDLibSelectorRemoveElementFromQueue:
			q->cookies.erase(cookie);
			break;
		default:
			args->scalarOutput[0] = q->cookies.count(cookie);
	}
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::startStopQueue(uint32_t selector, IOExternalMethodArguments* args)
{
	Queue* q = args->scalarInputCount >= 1 ? queue(args->scalarInput[0]) : nullptr;

	if (!q)
		return kIOReturnBadArgument;
	q->started = selector == kIOHIDLibSelectorStartQueue;
	return kIOReturnSuccess;
}

IOReturn IOHIDLibUserClient::clientMemory(uint32_t type, int* fd, uint64_t* size)
{
	Queue* q = queue(type);

	if (!q)
		return kIOReturnBadArgument;
	*fd = q->fd;
	*size = q->memorySize;
	return kIOReturnSuccess;
}

void IOHIDLibUserClient::enqueue(Queue& q, uint32_t cookie, uint64_t timestamp, uint32_t generation,
	const void* bytes, size_t length, uint32_t flags)
{
	size_t words = std::max<size_t>(alignWord(length) / 4, 1);
	size_t total = IOHIDLIB_ELEMENT_VALUE_HEADER + words * 4;
	std::vector<uint8_t> buffer(total, 0);
	IOHIDLibElementValue* value = reinterpret_cast<IOHIDLibElementValue*>(buffer.data());

	value->cookie = cookie;
	value->flagsAndSize = flags | (uint32_t(total) << 8);
	value->timestampLow = uint32_t(timestamp);
	value->timestampHigh = uint32_t(timestamp >> 32);
	value->generation = generation;
	std::memcpy(value->value, bytes, length);
	// A full queue drops the entry: the client is not keeping up
	IODataQueueEnqueue(q.memory, buffer.data(), uint32_t(total));
}

void IOHIDLibUserClient::reportChanged(uint32_t reportID, const std::vector<uint8_t>& previous, const std::vector<uint8_t>& current)
{
	uint64_t stamp = now();
	const auto& elements = m_device->hidElements();
	std::set<uint32_t> changed;

	m_generation++;
	for (uint32_t cookie = 0; cookie < m_cookies.size(); cookie++)
	{
		const ElementRef& ref = m_cookies[cookie];
		if (ref.description == size_t(-1))
			continue;

		const IOHIDLibElement& wire = elements[ref.description].wire;
		if (elements[ref.description].reportHandler || wire.type == kIOHIDLibTypeCollection
			|| wire.type == kIOHIDLibTypeOutput || wire.type == kIOHIDLibTypeFeature || wire.reportID != reportID)
			continue;

		int64_t value = valueOf(ref, current);
		// A relative value (mouse motion) is an event in every report that carries a motion
		if (valueOf(ref, previous) != value || ((wire.flags & kIOHIDLibFlagsRelative) && value != 0))
		{
			changed.insert(cookie);
			m_times[cookie] = stamp;
		}
	}

	std::vector<uint8_t> withID = inputReportWithID(reportID);
	for (auto& [token, q] : m_queues)
	{
		if (!q->started)
			continue;
		for (uint32_t cookie : q->cookies)
		{
			const ElementRef* ref = find(cookie);
			if (elements[ref->description].reportHandler)
			{
				if (elements[ref->description].wire.reportID == reportID)
					enqueue(*q, cookie, stamp, m_generation, withID.data(), withID.size(), 0);
			}
			else if (changed.count(cookie))
			{
				uint32_t word = uint32_t(valueOf(*ref, current));
				enqueue(*q, cookie, stamp, m_generation, &word, sizeof(word), 0);
			}
		}
	}
}
