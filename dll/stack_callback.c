#include "oc_api.h"
#include "oc_core_res.h"
#include "oc_knx_fp.h"
#include "stack_callback.h"

#define MY_NAME "Actuator (LSAB) 417" /**< The name of the application */

int iot_device_init(void)
{
  /* set the manufacturer name */
  int ret = oc_init_platform("Cascoda", NULL, NULL);

  /* set the application name, version, base url, device serial number */
  ret |= oc_add_device(MY_NAME, "1.0.0", "//", "00FA10010701", NULL, NULL);

  oc_device_info_t *device = oc_core_get_device_info(0);
  PRINT("Serial Number: %s\n", oc_string_checked(device->serialnumber));

  /* set the hardware version 1.0.0 */
  oc_core_set_device_hwv(0, 1, 0, 0);

  /* set the firmware version 1.0.0 */
  oc_core_set_device_fwv(0, 1, 0, 0);

  /* set the hardware type*/
  oc_core_set_device_hwt(0, "Pi");

  /* set the application info*/
  oc_core_set_device_ap(0, 1, 0, 0);

  /* set the manufacturer info*/
  oc_core_set_device_mid(0, 12);

  /* set the model */
  oc_core_set_device_model(0, "Cascoda Actuator");

  #define PASSWORD "LETTUCE"
  oc_spake_set_password(PASSWORD);
  PRINT(" SPAKE password %s\n", PASSWORD);

  return ret;
}

int iot_device_get_lsm_s()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return device->lsm_s;
}

int iot_device_get_ia()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return device->ia;
}

bool iot_device_get_pm()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return device->pm;
}

char* iot_device_get_hostname()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return oc_string(device->hostname);
}

char* iot_device_get_serialnumber()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return oc_string(device->serialnumber);
}

int iot_device_get_iid()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return device->iid;
}

int iot_device_get_grouptable_length()
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  return oc_core_get_group_object_table_total_size();
}

int iot_device_get_grouptable_entry_id(int index)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_group_object_table_t *entry = oc_core_get_group_object_table_entry(index);
  return entry->id;
}

char* iot_device_get_grouptable_entry_href(int index)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_group_object_table_t *entry = oc_core_get_group_object_table_entry(index);
  return oc_string(entry->href);
}

int* iot_device_get_grouptable_entry_ga(int index)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_group_object_table_t *entry = oc_core_get_group_object_table_entry(index);
  return entry->ga;
}

int iot_device_get_grouptable_entry_ga_length(int index)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_group_object_table_t *entry = oc_core_get_group_object_table_entry(index);
  return entry->ga_len;
}

int iot_device_get_grouptable_entry_cflags(int index)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_group_object_table_t *entry = oc_core_get_group_object_table_entry(index);
  return entry->cflags;
}

void iot_device_set_pm(bool programmingMode)
{
  oc_device_info_t *device = oc_core_get_device_info(0);
  oc_core_set_device_pm(0, programmingMode);
}

void iot_device_issues_mode(char *url, char *serviceType)
{
  oc_do_s_mode_with_scope(2, url, serviceType);
  oc_do_s_mode_with_scope(5, url, serviceType);
}

void oc_add_s_mode_response_cb(char *url, oc_rep_t *rep, oc_rep_t *rep_value)
{
  (void)rep;
  (void)rep_value;

  PRINT("oc_add_s_mode_response_cb %s\n", url);
}

