// Extracted and modified for Darling by Lubos Dolezel
/*
 * Copyright (c) 1998-2019 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#ifndef IOKITD_IOUSERCLIENT_MIG_H
#define IOKITD_IOUSERCLIENT_MIG_H
#include <IOKit/IOReturn.h>
#include "IOService.h"

enum {
	kIOExternalMethodScalarInputCountMax  = 16,
	kIOExternalMethodScalarOutputCountMax = 16,
};

enum {
	kIOUCVariableStructureSize = 0xffffffff
};

enum {
	kIOUCTypeMask       = 0x0000000f,
	kIOUCScalarIScalarO = 0,
	kIOUCScalarIStructO = 2,
	kIOUCStructIStructO = 3,
	kIOUCScalarIStructI = 4,

	kIOUCForegroundOnly = 0x00000010,
};

struct IOExternalMethodArguments {
	uint32_t            version;

	uint32_t            selector;

	//mach_port_t           asyncWakePort;
	//io_user_reference_t * asyncReference;
	//uint32_t              asyncReferenceCount;

	const uint64_t *    scalarInput;
	uint32_t            scalarInputCount;

	const void *        structureInput;
	uint32_t            structureInputSize;

	//IOMemoryDescriptor * structureInputDescriptor;

	uint64_t *          scalarOutput;
	uint32_t            scalarOutputCount;

	void *              structureOutput;
	uint32_t            structureOutputSize;

	//IOMemoryDescriptor * structureOutputDescriptor;
	uint32_t             structureOutputDescriptorSize;

	uint32_t            __reservedA;

	void **         structureVariableOutputData;

	uint32_t            __reserved[30];
};

//typedef IOReturn (*IOExternalMethodAction)(OSObject * target, void * reference,
//    IOExternalMethodArguments * arguments);
//struct IOExternalMethodDispatch {
//	IOExternalMethodAction function;
//	uint32_t               checkScalarInputCount;
//	uint32_t               checkStructureInputSize;
//	uint32_t               checkScalarOutputCount;
//	uint32_t               checkStructureOutputSize;
//};

typedef IOReturn (IOService::*IOMethod)(void * p1, void * p2, void * p3,
    void * p4, void * p5, void * p6 );

struct IOExternalMethod {
	IOService *         object;
	IOMethod            func;
	IOOptionBits        flags;
	IOByteCount         count0;
	IOByteCount         count1;
};

enum {
#define IO_EXTERNAL_METHOD_ARGUMENTS_CURRENT_VERSION    2
	kIOExternalMethodArgumentsCurrentVersion = IO_EXTERNAL_METHOD_ARGUMENTS_CURRENT_VERSION
};

#endif
