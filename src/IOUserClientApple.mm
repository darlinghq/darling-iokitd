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

extern "C" {
#include "iokitmigServer.h"
#include "iokitdarlingServer.h"
}
#include <sys/fileport.h>
#include "IOUserClientApple.h"
#include "IOUserClient.h"
#include <cstdio>

#define DTRACE_IO2(...)
#define IOLog printf

#define SCALAR64(x) ((io_user_scalar_t)((unsigned int)x))
#define SCALAR32(x) ((uint32_t )x)
#define ARG32(x)    ((void *)(uintptr_t)SCALAR32(x))
#define REF64(x)    ((io_user_reference_t)((UInt64)(x)))
#define REF32(x)    ((int)(x))

kern_return_t
shim_io_connect_method_scalarI_structureI(
	IOExternalMethod *  method,
	IOService *         object,
	const io_user_scalar_t * input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t              inputStruct,
	mach_msg_type_number_t  inputStructCount )
{
	IOMethod            func;
	IOReturn            err = kIOReturnBadArgument;

	do{
		if (inputCount != method->count0) {
			IOLog("%s:%d %s: IOUserClient inputCount count mismatch 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)inputCount, (uint64_t)method->count0);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)inputCount, uint64_t, (uint64_t)method->count0);
			continue;
		}
		if ((kIOUCVariableStructureSize != method->count1)
		    && (inputStructCount != method->count1)) {
			IOLog("%s:%d %s: IOUserClient outputCount count mismatch 0x%llx 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)inputStructCount, (uint64_t)method->count1, (uint64_t)kIOUCVariableStructureSize);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)inputStructCount, uint64_t, (uint64_t)method->count1);
			continue;
		}

		func = method->func;

		switch (inputCount) {
		case 5:
			err = (object->*func)( ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]), ARG32(input[4]),
			    inputStruct );
			break;
		case 4:
			err = (object->*func)( ARG32(input[0]), ARG32(input[1]), (void *)  input[2],
			    ARG32(input[3]),
			    inputStruct, (void *)(uintptr_t)inputStructCount );
			break;
		case 3:
			err = (object->*func)( ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    inputStruct, (void *)(uintptr_t)inputStructCount,
			    NULL );
			break;
		case 2:
			err = (object->*func)( ARG32(input[0]), ARG32(input[1]),
			    inputStruct, (void *)(uintptr_t)inputStructCount,
			    NULL, NULL );
			break;
		case 1:
			err = (object->*func)( ARG32(input[0]),
			    inputStruct, (void *)(uintptr_t)inputStructCount,
			    NULL, NULL, NULL );
			break;
		case 0:
			err = (object->*func)( inputStruct, (void *)(uintptr_t)inputStructCount,
			    NULL, NULL, NULL, NULL );
			break;

		default:
			IOLog("%s: Bad method table\n", object->getName().c_str());
		}
	}while (false);

	return err;
}

kern_return_t
shim_io_connect_method_structureI_structureO(
	IOExternalMethod *  method,
	IOService *         object,
	io_struct_inband_t              input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t              output,
	IOByteCount *   outputCount )
{
	IOMethod            func;
	IOReturn            err = kIOReturnBadArgument;

	do{
		if ((kIOUCVariableStructureSize != method->count0)
		    && (inputCount != method->count0)) {
			IOLog("%s:%d %s: IOUserClient inputCount count mismatch 0x%llx 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)inputCount, (uint64_t)method->count0, (uint64_t)kIOUCVariableStructureSize);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)inputCount, uint64_t, (uint64_t)method->count0);
			continue;
		}
		if ((kIOUCVariableStructureSize != method->count1)
		    && (*outputCount != method->count1)) {
			IOLog("%s:%d %s: IOUserClient outputCount count mismatch 0x%llx 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)*outputCount, (uint64_t)method->count1, (uint64_t)kIOUCVariableStructureSize);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)*outputCount, uint64_t, (uint64_t)method->count1);
			continue;
		}

		func = method->func;

		if (method->count1) {
			if (method->count0) {
				err = (object->*func)( input, output,
				    (void *)(uintptr_t)inputCount, outputCount, NULL, NULL );
			} else {
				err = (object->*func)( output, outputCount, NULL, NULL, NULL, NULL );
			}
		} else {
			err = (object->*func)( input, (void *)(uintptr_t)inputCount, NULL, NULL, NULL, NULL );
		}
	}while (false);


	return err;
}

kern_return_t
shim_io_connect_method_scalarI_scalarO(
	IOExternalMethod *      method,
	IOService *             object,
	const io_user_scalar_t * input,
	mach_msg_type_number_t   inputCount,
	io_user_scalar_t * output,
	mach_msg_type_number_t * outputCount )
{
	IOMethod            func;
	io_scalar_inband_t  _output;
	IOReturn            err;
	err = kIOReturnBadArgument;

	bzero(&_output[0], sizeof(_output));
	do {
		if (inputCount != method->count0) {
			IOLog("%s:%d %s: IOUserClient inputCount count mismatch 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)inputCount, (uint64_t)method->count0);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)inputCount, uint64_t, (uint64_t)method->count0);
			continue;
		}
		if (*outputCount != method->count1) {
			IOLog("%s:%d %s: IOUserClient outputCount count mismatch 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)*outputCount, (uint64_t)method->count1);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)*outputCount, uint64_t, (uint64_t)method->count1);
			continue;
		}

		func = method->func;

		switch (inputCount) {
		case 6:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]), ARG32(input[4]), ARG32(input[5]));
			break;
		case 5:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]), ARG32(input[4]),
			    &_output[0] );
			break;
		case 4:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]),
			    &_output[0], &_output[1] );
			break;
		case 3:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    &_output[0], &_output[1], &_output[2] );
			break;
		case 2:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]),
			    &_output[0], &_output[1], &_output[2],
			    &_output[3] );
			break;
		case 1:
			err = (object->*func)(  ARG32(input[0]),
			    &_output[0], &_output[1], &_output[2],
			    &_output[3], &_output[4] );
			break;
		case 0:
			err = (object->*func)(  &_output[0], &_output[1], &_output[2],
			    &_output[3], &_output[4], &_output[5] );
			break;

		default:
			IOLog("%s: Bad method table\n", object->getName().c_str());
		}
	}while (false);

	uint32_t i;
	for (i = 0; i < *outputCount; i++) {
		output[i] = SCALAR32(_output[i]);
	}

	return err;
}

kern_return_t
shim_io_connect_method_scalarI_structureO(

	IOExternalMethod *      method,
	IOService *             object,
	const io_user_scalar_t * input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t              output,
	IOByteCount *   outputCount )
{
	IOMethod            func;
	IOReturn            err;

	err = kIOReturnBadArgument;

	do {
		if (inputCount != method->count0) {
			IOLog("%s:%d %s: IOUserClient inputCount count mismatch 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)inputCount, (uint64_t)method->count0);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)inputCount, uint64_t, (uint64_t)method->count0);
			continue;
		}
		if ((kIOUCVariableStructureSize != method->count1)
		    && (*outputCount != method->count1)) {
			IOLog("%s:%d %s: IOUserClient outputCount count mismatch 0x%llx 0x%llx 0x%llx\n", __FUNCTION__, __LINE__, object->getName().c_str(), (uint64_t)*outputCount, (uint64_t)method->count1, (uint64_t)kIOUCVariableStructureSize);
			DTRACE_IO2(iokit_count_mismatch, uint64_t, (uint64_t)*outputCount, uint64_t, (uint64_t)method->count1);
			continue;
		}

		func = method->func;

		switch (inputCount) {
		case 5:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]), ARG32(input[4]),
			    output );
			break;
		case 4:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    ARG32(input[3]),
			    output, (void *)outputCount );
			break;
		case 3:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]), ARG32(input[2]),
			    output, (void *)outputCount, NULL );
			break;
		case 2:
			err = (object->*func)(  ARG32(input[0]), ARG32(input[1]),
			    output, (void *)outputCount, NULL, NULL );
			break;
		case 1:
			err = (object->*func)(  ARG32(input[0]),
			    output, (void *)outputCount, NULL, NULL, NULL );
			break;
		case 0:
			err = (object->*func)(  output, (void *)outputCount, NULL, NULL, NULL, NULL );
			break;

		default:
			IOLog("%s: Bad method table\n", object->getName().c_str());
		}
	}while (false);

	return err;
}

IOExternalMethod *
IOUserClient::
getTargetAndMethodForIndex(IOService **targetP, UInt32 index)
{
	IOExternalMethod *method = getExternalMethodForIndex(index);

	if (method) {
		*targetP = (IOService *) method->object;
	}

	return method;
}

IOReturn
IOUserClient::asyncExternalMethod(uint32_t selector, IOExternalMethodArguments * args,
	mach_port_t wakePort, const uint64_t * reference, uint32_t referenceCount)
{
	if (MACH_PORT_VALID(wakePort))
		mach_port_deallocate(mach_task_self(), wakePort);
	return kIOReturnUnsupported;
}

IOReturn
IOUserClient::clientMemory(uint32_t type, int * fd, uint64_t * size)
{
	return kIOReturnUnsupported;
}

IOReturn
IOUserClient::externalMethod( uint32_t selector, IOExternalMethodArguments * args)
{
	IOReturn    err;
	IOService * object;
	IOByteCount structureOutputSize;


	structureOutputSize = args->structureOutputSize;

 {
		IOExternalMethod *      method;
		object = NULL;
		if (!(method = getTargetAndMethodForIndex(&object, selector)) || !object) {
			return kIOReturnUnsupported;
		}

		switch (method->flags & kIOUCTypeMask) {
		case kIOUCScalarIStructI:
			err = shim_io_connect_method_scalarI_structureI( method, object,
			    args->scalarInput, args->scalarInputCount,
			    (char *) args->structureInput, args->structureInputSize );
			break;

		case kIOUCScalarIScalarO:
			err = shim_io_connect_method_scalarI_scalarO( method, object,
			    args->scalarInput, args->scalarInputCount,
			    args->scalarOutput, &args->scalarOutputCount );
			break;

		case kIOUCScalarIStructO:
			err = shim_io_connect_method_scalarI_structureO( method, object,
			    args->scalarInput, args->scalarInputCount,
			    (char *) args->structureOutput, &structureOutputSize );
			break;


		case kIOUCStructIStructO:
			err = shim_io_connect_method_structureI_structureO( method, object,
			    (char *) args->structureInput, args->structureInputSize,
			    (char *) args->structureOutput, &structureOutputSize );
			break;

		default:
			err = kIOReturnBadArgument;
			break;
		}
	}

	args->structureOutputSize = structureOutputSize;

	return err;
}

// IOConnectCallMethod() and IOConnectCallAsyncMethod(). A wakePort other than
// MACH_PORT_NULL makes the call asynchronous.
static kern_return_t
connect_method
(
	mach_port_t connection,
	mach_port_t wakePort,
	const uint64_t * reference,
	uint32_t referenceCount,
	uint32_t selector,
	io_scalar_inband64_t scalar_input,
	mach_msg_type_number_t scalar_inputCnt,
	io_struct_inband_t inband_input,
	mach_msg_type_number_t inband_inputCnt,
	mach_vm_address_t ool_input,
	mach_vm_size_t ool_input_size,
	io_struct_inband_t inband_output,
	mach_msg_type_number_t *inband_outputCnt,
	io_scalar_inband64_t scalar_output,
	mach_msg_type_number_t *scalar_outputCnt,
	mach_vm_address_t ool_output,
	mach_vm_size_t *ool_output_size
)
{
	//CHECK( IOUserClient, connection, client );
	IOUserClient* client = dynamic_cast<IOUserClient*>(IOObject::lookup(connection));
	if (!client)
	{
		if (MACH_PORT_VALID(wakePort))
			mach_port_deallocate(mach_task_self(), wakePort);
		return kIOReturnBadArgument;
	}

	IOExternalMethodArguments args;
	IOReturn ret;
	//IOMemoryDescriptor * inputMD  = NULL;
	//IOMemoryDescriptor * outputMD = NULL;

	bzero(&args.__reserved[0], sizeof(args.__reserved));
	args.__reservedA = 0;
	args.version = kIOExternalMethodArgumentsCurrentVersion;
	args.structureOutputDescriptorSize = 0;

	args.selector = selector;

	//args.asyncWakePort               = MACH_PORT_NULL;
	//args.asyncReference              = NULL;
	//args.asyncReferenceCount         = 0;
	args.structureVariableOutputData = NULL;

	args.scalarInput = scalar_input;
	args.scalarInputCount = scalar_inputCnt;
	args.structureInput = inband_input;
	args.structureInputSize = inband_inputCnt;

	if ((ool_input && (ool_input_size <= sizeof(io_struct_inband_t)))
		|| (ool_output && (*ool_output_size <= sizeof(io_struct_inband_t)))) {
		if (MACH_PORT_VALID(wakePort))
			mach_port_deallocate(mach_task_self(), wakePort);
		return kIOReturnIPCError;
	}

	if (ool_input) {
		/*inputMD = IOMemoryDescriptor::withAddressRange(ool_input, ool_input_size,
		    kIODirectionOut | kIOMemoryMapCopyOnWrite,
		    current_task());*/
	}

	//args.structureInputDescriptor = inputMD;

	args.scalarOutput = scalar_output;
	args.scalarOutputCount = *scalar_outputCnt;
	bzero(&scalar_output[0], *scalar_outputCnt * sizeof(scalar_output[0]));
	args.structureOutput = inband_output;
	args.structureOutputSize = *inband_outputCnt;

	if (ool_output && ool_output_size) {
		/*outputMD = IOMemoryDescriptor::withAddressRange(ool_output, *ool_output_size,
		    kIODirectionIn, current_task());*/
	}

	//args.structureOutputDescriptor = outputMD;
	//args.structureOutputDescriptorSize = ool_output_size ? *ool_output_size : 0;

	//IOStatisticsClientCall();
	if (MACH_PORT_VALID(wakePort))
		ret = client->asyncExternalMethod( selector, &args, wakePort, reference, referenceCount );
	else
		ret = client->externalMethod( selector, &args );

	*scalar_outputCnt = args.scalarOutputCount;
	*inband_outputCnt = args.structureOutputSize;
	*ool_output_size  = args.structureOutputDescriptorSize;

	/*if (inputMD) {
		inputMD->release();
	}
	if (outputMD) {
		outputMD->release();
	}*/

	return ret;
}



kern_return_t
is_io_connect_method
(
	mach_port_t connection,
	uint32_t selector,
	io_scalar_inband64_t scalar_input,
	mach_msg_type_number_t scalar_inputCnt,
	io_struct_inband_t inband_input,
	mach_msg_type_number_t inband_inputCnt,
	mach_vm_address_t ool_input,
	mach_vm_size_t ool_input_size,
	io_struct_inband_t inband_output,
	mach_msg_type_number_t *inband_outputCnt,
	io_scalar_inband64_t scalar_output,
	mach_msg_type_number_t *scalar_outputCnt,
	mach_vm_address_t ool_output,
	mach_vm_size_t *ool_output_size
)
{
	return connect_method(connection, MACH_PORT_NULL, NULL, 0, selector,
	    scalar_input, scalar_inputCnt, inband_input, inband_inputCnt, ool_input, ool_input_size,
	    inband_output, inband_outputCnt, scalar_output, scalar_outputCnt, ool_output, ool_output_size);
}

kern_return_t
is_io_connect_async_method
(
	mach_port_t connection,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	uint32_t selector,
	io_scalar_inband64_t scalar_input,
	mach_msg_type_number_t scalar_inputCnt,
	io_struct_inband_t inband_input,
	mach_msg_type_number_t inband_inputCnt,
	mach_vm_address_t ool_input,
	mach_vm_size_t ool_input_size,
	io_struct_inband_t inband_output,
	mach_msg_type_number_t *inband_outputCnt,
	io_scalar_inband64_t scalar_output,
	mach_msg_type_number_t *scalar_outputCnt,
	mach_vm_address_t ool_output,
	mach_vm_size_t *ool_output_size
)
{
	if (!MACH_PORT_VALID(wake_port))
		return kIOReturnBadArgument;
	return connect_method(connection, wake_port, reference, referenceCnt, selector,
	    scalar_input, scalar_inputCnt, inband_input, inband_inputCnt, ool_input, ool_input_size,
	    inband_output, inband_outputCnt, scalar_output, scalar_outputCnt, ool_output, ool_output_size);
}

kern_return_t
is_io_connect_method_scalarI_scalarO(
	mach_port_t        connect,
	uint32_t           index,
	io_scalar_inband_t       input,
	mach_msg_type_number_t   inputCount,
	io_scalar_inband_t       output,
	mach_msg_type_number_t * outputCount )
{
	IOReturn err;
	uint32_t i;
	io_scalar_inband64_t _input;
	io_scalar_inband64_t _output;

	mach_msg_type_number_t struct_outputCnt = 0;
	mach_vm_size_t ool_output_size = 0;

	bzero(&_output[0], sizeof(_output));
	for (i = 0; i < inputCount; i++) {
		_input[i] = SCALAR64(input[i]);
	}

	err = is_io_connect_method(connect, index,
	    _input, inputCount,
	    NULL, 0,
	    0, 0,
	    NULL, &struct_outputCnt,
	    _output, outputCount,
	    0, &ool_output_size);

	for (i = 0; i < *outputCount; i++) {
		output[i] = SCALAR32(_output[i]);
	}

	return err;
}

kern_return_t
is_io_connect_method_scalarI_structureO(
	mach_port_t     connect,
	uint32_t        index,
	io_scalar_inband_t input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t              output,
	mach_msg_type_number_t *        outputCount )
{
	uint32_t i;
	io_scalar_inband64_t _input;

	mach_msg_type_number_t scalar_outputCnt = 0;
	mach_vm_size_t ool_output_size = 0;

	for (i = 0; i < inputCount; i++) {
		_input[i] = SCALAR64(input[i]);
	}

	return is_io_connect_method(connect, index,
	           _input, inputCount,
	           NULL, 0,
	           0, 0,
	           output, outputCount,
	           NULL, &scalar_outputCnt,
	           0, &ool_output_size);
}

kern_return_t
is_io_connect_method_scalarI_structureI(
	mach_port_t            connect,
	uint32_t                index,
	io_scalar_inband_t      input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t      inputStruct,
	mach_msg_type_number_t  inputStructCount )
{
	uint32_t i;
	io_scalar_inband64_t _input;

	mach_msg_type_number_t scalar_outputCnt = 0;
	mach_msg_type_number_t inband_outputCnt = 0;
	mach_vm_size_t ool_output_size = 0;

	for (i = 0; i < inputCount; i++) {
		_input[i] = SCALAR64(input[i]);
	}

	return is_io_connect_method(connect, index,
	           _input, inputCount,
	           inputStruct, inputStructCount,
	           0, 0,
	           NULL, &inband_outputCnt,
	           NULL, &scalar_outputCnt,
	           0, &ool_output_size);
}

kern_return_t
is_io_connect_method_structureI_structureO(
	mach_port_t     connect,
	uint32_t        index,
	io_struct_inband_t              input,
	mach_msg_type_number_t  inputCount,
	io_struct_inband_t              output,
	mach_msg_type_number_t *        outputCount )
{
	mach_msg_type_number_t scalar_outputCnt = 0;
	mach_vm_size_t ool_output_size = 0;

	return is_io_connect_method(connect, index,
	           NULL, 0,
	           input, inputCount,
	           0, 0,
	           output, outputCount,
	           NULL, &scalar_outputCnt,
	           0, &ool_output_size);
}

// A client's request for the memory it wants to map (iokitdarling.defs): the
// client library maps the file descriptor of the memory that a fileport carries.
kern_return_t
isd_io_connect_client_memory
(
	mach_port_t connection,
	uint32_t memory_type,
	mach_port_t * memory,
	uint64_t * size
)
{
	IOUserClient* client = dynamic_cast<IOUserClient*>(IOObject::lookup(connection));
	int fd = -1;
	uint64_t length = 0;
	IOReturn ret;

	*memory = MACH_PORT_NULL;
	*size = 0;
	if (!client)
		return kIOReturnBadArgument;

	ret = client->clientMemory(memory_type, &fd, &length);
	if (ret != kIOReturnSuccess)
		return ret;
	if (fd < 0 || fileport_makeport(fd, memory) != 0)
		return kIOReturnNoResources;

	*size = length;
	return kIOReturnSuccess;
}
