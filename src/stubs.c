#include "iokitmigServer.h"
#include <os/log.h>

#define STUB() os_log(OS_LOG_DEFAULT, "%d STUB called: %s", getpid(), __FUNCTION__); printf("STUB called: %s\n", __FUNCTION__)

kern_return_t is_io_registry_entry_get_property
(
	mach_port_t registry_entry,
	io_name_t property_name,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_iterator_enter_entry
(
	mach_port_t iterator
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_iterator_exit_entry
(
	mach_port_t iterator
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_get_property_bytes
(
	mach_port_t registry_entry,
	io_name_t property_name,
	io_struct_inband_t data,
	mach_msg_type_number_t *dataCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_close
(
	mach_port_t connection
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_get_service
(
	mach_port_t connection,
	mach_port_t *service
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_set_notification_port
(
	mach_port_t connection,
	uint32_t notification_type,
	mach_port_t port,
	uint32_t reference
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_map_memory
(
	mach_port_t connection,
	uint32_t memory_type,
	task_t into_task,
	uint32_t *address,
	uint32_t *size,
	uint32_t flags
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_add_client
(
	mach_port_t connection,
	mach_port_t connect_to
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_set_properties
(
	mach_port_t connection,
	io_buf_ptr_t properties,
	mach_msg_type_number_t propertiesCnt,
	kern_return_t *result
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method_scalarI_scalarO
(
	mach_port_t connection,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_scalar_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method_scalarI_structureO
(
	mach_port_t connection,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method_scalarI_structureI
(
	mach_port_t connection,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t inputStruct,
	mach_msg_type_number_t inputStructCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method_structureI_structureO
(
	mach_port_t connection,
	uint32_t selector,
	io_struct_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


kern_return_t is_io_registry_entry_set_properties
(
	mach_port_t registry_entry,
	io_buf_ptr_t properties,
	mach_msg_type_number_t propertiesCnt,
	kern_return_t *result
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_in_plane
(
	mach_port_t registry_entry,
	io_name_t plane,
	boolean_t *inPlane
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_busy_state
(
	mach_port_t service,
	uint32_t *busyState
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_wait_quiet
(
	mach_port_t service,
	mach_timespec_t wait_time
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_send_data
(
	mach_port_t master_port,
	uint32_t flag,
	io_buf_ptr_t inData,
	mach_msg_type_number_t inDataCnt,
	kern_return_t *result
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_terminate
(
	mach_port_t master_port,
	uint32_t flag,
	io_name_t name
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_get_data
(
	mach_port_t master_port,
	uint32_t flag,
	io_buf_ptr_t *outData,
	mach_msg_type_number_t *outDataCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_get_gen_count
(
	mach_port_t master_port,
	uint32_t *genCount
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_module_loaded
(
	mach_port_t master_port,
	io_name_t name
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_catalog_reset
(
	mach_port_t master_port,
	uint32_t flag
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_request_probe
(
	mach_port_t service,
	uint32_t options
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_match_property_table
(
	mach_port_t service,
	io_string_t matching,
	boolean_t *matches
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_async_method_scalarI_scalarO
(
	mach_port_t connection,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_scalar_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_async_method_scalarI_structureO
(
	mach_port_t connection,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_async_method_scalarI_structureI
(
	mach_port_t connection,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	uint32_t selector,
	io_scalar_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t inputStruct,
	mach_msg_type_number_t inputStructCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_async_method_structureI_structureO
(
	mach_port_t connection,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	uint32_t selector,
	io_struct_inband_t input,
	mach_msg_type_number_t inputCnt,
	io_struct_inband_t output,
	mach_msg_type_number_t *outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_string_t matching,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_interest_notification
(
	mach_port_t service,
	io_name_t type_of_interest,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_acknowledge_notification
(
	mach_port_t service,
	natural_t notify_ref,
	natural_t response
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_get_notification_semaphore
(
	mach_port_t connection,
	natural_t notification_type,
	semaphore_t *semaphore
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_unmap_memory
(
	mach_port_t connection,
	uint32_t memory_type,
	task_t into_task,
	uint32_t address
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_get_location_in_plane
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t location
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_get_property_recursively
(
	mach_port_t registry_entry,
	io_name_t plane,
	io_name_t property_name,
	uint32_t options,
	io_buf_ptr_t *properties,
	mach_msg_type_number_t *propertiesCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_state
(
	mach_port_t service,
	uint64_t *state,
	uint32_t *busy_state,
	uint64_t *accumulated_busy_time
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_match_property_table_ool
(
	mach_port_t service,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	kern_return_t *result,
	boolean_t *matches
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification_ool
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	kern_return_t *result,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_object_get_superclass
(
	mach_port_t master_port,
	io_name_t obj_name,
	io_name_t class_name
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_object_get_bundle_identifier
(
	mach_port_t master_port,
	io_name_t obj_name,
	io_name_t class_name
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_open_extended
(
	mach_port_t service,
	task_t owningTask,
	uint32_t connect_type,
	NDR_record_t ndr,
	io_buf_ptr_t properties,
	mach_msg_type_number_t propertiesCnt,
	kern_return_t *result,
	mach_port_t *connection
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_map_memory_into_task
(
	mach_port_t connection,
	uint32_t memory_type,
	task_t into_task,
	mach_vm_address_t *address,
	mach_vm_size_t *size,
	uint32_t flags
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_unmap_memory_from_task
(
	mach_port_t connection,
	uint32_t memory_type,
	task_t from_task,
	mach_vm_address_t address
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method
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
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_async_method
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
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_set_notification_port_64
(
	mach_port_t connection,
	uint32_t notification_type,
	mach_port_t port,
	io_user_reference_t reference
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification_64
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_string_t matching,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_interest_notification_64
(
	mach_port_t service,
	io_name_t type_of_interest,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification_ool_64
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	kern_return_t *result,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_get_registry_entry_id
(
	mach_port_t registry_entry,
	uint64_t *entry_id
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_connect_method_var_output
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
	io_buf_ptr_t *var_output,
	mach_msg_type_number_t *var_outputCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_matching_service
(
	mach_port_t master_port,
	io_string_t matching,
	mach_port_t *service
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_matching_service_ool
(
	mach_port_t master_port,
	io_buf_ptr_t matching,
	mach_msg_type_number_t matchingCnt,
	kern_return_t *result,
	mach_port_t *service
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_get_authorization_id
(
	mach_port_t service,
	uint64_t *authorization_id
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


kern_return_t is_io_service_set_authorization_id
(
	mach_port_t service,
	uint64_t authorization_id
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_server_version
(
	mach_port_t master_port,
	uint64_t *version
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


kern_return_t is_io_service_get_matching_service_bin
(
	mach_port_t master_port,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t *service
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_match_property_table_bin
(
	mach_port_t service,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	boolean_t *matches
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification_bin
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_service_add_notification_bin_64
(
	mach_port_t master_port,
	io_name_t notification_type,
	io_struct_inband_t matching,
	mach_msg_type_number_t matchingCnt,
	mach_port_t wake_port,
	io_async_ref64_t reference,
	mach_msg_type_number_t referenceCnt,
	mach_port_t *notification
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_from_path_ool
(
	mach_port_t master_port,
	io_string_inband_t path,
	io_buf_ptr_t path_ool,
	mach_msg_type_number_t path_oolCnt,
	kern_return_t *result,
	mach_port_t *registry_entry
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

kern_return_t is_io_registry_entry_get_path_ool
 (
    mach_port_t registry_entry,
    io_name_t plane,
    io_string_inband_t path,
    io_buf_ptr_t *path_ool,
    mach_msg_type_number_t *path_oolCnt
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}
