#include "powermanagementServer.h"
#include <os/log.h>

#define STUB() os_log(OS_LOG_DEFAULT, "%d STUB called: %s", getpid(), __FUNCTION__); printf("STUB called: %s\n", __FUNCTION__)

/* Routine io_pm_get_value_int */

kern_return_t _io_pm_get_value_int
(
	mach_port_t server,
	audit_token_t token,
	int selector,
	int *value
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_value_int */

kern_return_t _io_pm_set_value_int
(
	mach_port_t server,
	audit_token_t token,
	int selector,
	int value,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_force_active_settings */

kern_return_t _io_pm_force_active_settings
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t profiles,
	mach_msg_type_number_t profilesCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_active_profile */

kern_return_t _io_pm_set_active_profile
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t profiles,
	mach_msg_type_number_t profilesCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_schedule_power_event */

kern_return_t _io_pm_schedule_power_event
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t package,
	mach_msg_type_number_t packageCnt,
	int action,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_schedule_repeat_event */

kern_return_t _io_pm_schedule_repeat_event
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t package,
	mach_msg_type_number_t packageCnt,
	int action,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_cancel_repeat_events */

kern_return_t _io_pm_cancel_repeat_events
(
	mach_port_t server,
	audit_token_t token,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_last_wake_time */

kern_return_t _io_pm_last_wake_time
(
	mach_port_t server,
	vm_offset_t *wakeData,
	mach_msg_type_number_t *wakeDataCnt,
	vm_offset_t *deltaData,
	mach_msg_type_number_t *deltaDataCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_assertion_set_properties */

kern_return_t _io_pm_assertion_set_properties
(
	mach_port_t server,
	audit_token_t token,
	int assertion_id,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *disableAppSleep,
	int *enableAppSleep,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

/* Routine io_pm_assertion_copy_details */

kern_return_t _io_pm_assertion_copy_details
(
	mach_port_t server,
	audit_token_t token,
	int assertion_id,
	int whichData,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	vm_offset_t *assertions,
	mach_msg_type_number_t *assertionsCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_declare_system_active */

kern_return_t _io_pm_declare_system_active
(
	mach_port_t server,
	audit_token_t token,
	int *state,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *assertion_id,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_declare_user_active */

kern_return_t _io_pm_declare_user_active
(
	mach_port_t server,
	audit_token_t token,
	int user_type,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *assertion_id,
	int *disableAppSleep,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_declare_network_client_active */

kern_return_t _io_pm_declare_network_client_active
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t props,
	mach_msg_type_number_t propsCnt,
	int *assertion_id,
	int *disableAppSleep,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_get_uuid */

kern_return_t _io_pm_get_uuid
(
	mach_port_t server,
	int selector,
	string_t out_uuid,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_connection_create */

kern_return_t _io_pm_connection_create
(
	mach_port_t server,
	mach_port_t task_in,
	string_t name,
	int interests,
	uint32_t *connection_id,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_connection_schedule_notification */

kern_return_t _io_pm_connection_schedule_notification
(
	mach_port_t server,
	uint32_t connection_id,
	mach_port_t notify_port,
	int disable,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_connection_release */

kern_return_t _io_pm_connection_release
(
	mach_port_t server,
	uint32_t connection_id,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_connection_acknowledge_event */

kern_return_t _io_pm_connection_acknowledge_event
(
	mach_port_t server,
	uint32_t connection_id,
	int messageToken,
	vm_offset_t options,
	mach_msg_type_number_t optionsCnt,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_connection_copy_status */

kern_return_t _io_pm_connection_copy_status
(
	mach_port_t server,
	int status_index,
	vm_offset_t *status_data,
	mach_msg_type_number_t *status_dataCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_ps_new_pspowersource */

kern_return_t _io_ps_new_pspowersource
(
	mach_port_t server,
	audit_token_t token,
	int *psid,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_ps_update_pspowersource */

kern_return_t _io_ps_update_pspowersource
(
	mach_port_t server,
	audit_token_t token,
	int psid,
	vm_offset_t psdetails,
	mach_msg_type_number_t psdetailsCnt,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_ps_release_pspowersource */

kern_return_t _io_ps_release_pspowersource
(
	mach_port_t server,
	audit_token_t token,
	int psid
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_ps_copy_powersources_info */

kern_return_t _io_ps_copy_powersources_info
(
	mach_port_t server,
	int pstype,
	vm_offset_t *powersources,
	mach_msg_type_number_t *powersourcesCnt,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_ps_copy_chargelog */

kern_return_t _io_ps_copy_chargelog
(
	mach_port_t server,
	audit_token_t token,
	double time,
	vm_offset_t *log,
	mach_msg_type_number_t *logCnt,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_hid_event_report_activity */

kern_return_t _io_pm_hid_event_report_activity
(
	mach_port_t server,
	audit_token_t token,
	int _action,
	int *allow
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_hid_event_copy_history */

kern_return_t _io_pm_hid_event_copy_history
(
	mach_port_t server,
	vm_offset_t *eventArray,
	mach_msg_type_number_t *eventArrayCnt,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_debug_flags */

kern_return_t _io_pm_set_debug_flags
(
	mach_port_t server,
	audit_token_t token,
	uint32_t newFlags,
	uint32_t setMode,
	uint32_t *oldFlags,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_bt_wake_interval */

kern_return_t _io_pm_set_bt_wake_interval
(
	mach_port_t server,
	audit_token_t token,
	uint32_t newInterval,
	uint32_t *oldInterval,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_dw_linger_interval */

kern_return_t _io_pm_set_dw_linger_interval
(
	mach_port_t server,
	audit_token_t token,
	uint32_t newInterval,
	uint32_t *oldInterval,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_change_sa_assertion_behavior */

kern_return_t _io_pm_change_sa_assertion_behavior
(
	mach_port_t server,
	audit_token_t token,
	uint32_t newFlags,
	uint32_t *oldFlags,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_set_sleepservice_wake_time_cap */

kern_return_t _io_pm_set_sleepservice_wake_time_cap
(
	mach_port_t server,
	audit_token_t token,
	int cap_time,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_get_capability_bits */

kern_return_t _io_pm_get_capability_bits
(
	mach_port_t server,
	audit_token_t token,
	uint32_t *capBits,
	int *return_val
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_ctl_assertion_type */

kern_return_t _io_pm_ctl_assertion_type
(
	mach_port_t server,
	audit_token_t token,
	string_t assertion,
	int action,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_assertion_notify */

kern_return_t _io_pm_assertion_notify
(
	mach_port_t server,
	audit_token_t token,
	string_t name,
	int req_type,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_assertion_activity_log */

kern_return_t _io_pm_assertion_activity_log
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t *log,
	mach_msg_type_number_t *logCnt,
	uint32_t *entryCnt,
	uint32_t *overflow,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}


/* Routine io_pm_assertion_activity_aggregate */

kern_return_t _io_pm_assertion_activity_aggregate
(
	mach_port_t server,
	audit_token_t token,
	vm_offset_t *statsData,
	mach_msg_type_number_t *statsDataCnt,
	int *return_code
)
{
    STUB();
    return KERN_NOT_SUPPORTED;
}

