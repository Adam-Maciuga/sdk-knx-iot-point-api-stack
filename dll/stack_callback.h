
#ifndef STACKCALLBACK_H
#define STACKCALLBACK_H

// use the CMake (generated) DLL export macros on WIN32
#include "kis-link_export.h"

kis_link_EXPORT int iot_device_init();

kis_link_EXPORT int iot_device_get_lsm_s();

kis_link_EXPORT int iot_device_get_lsm_s();

kis_link_EXPORT int iot_device_get_da();

kis_link_EXPORT bool iot_device_get_pm();

kis_link_EXPORT char *iot_device_get_hostname();

kis_link_EXPORT char *iot_device_get_serialnumber();

kis_link_EXPORT int iot_device_get_ia();

kis_link_EXPORT int iot_device_get_iid();

kis_link_EXPORT int iot_device_get_grouptable_length();

kis_link_EXPORT int iot_device_get_grouptable_entry_id(int index);

kis_link_EXPORT char *iot_device_get_grouptable_entry_href(int index);

kis_link_EXPORT int *iot_device_get_grouptable_entry_ga(int index);

kis_link_EXPORT int iot_device_get_grouptable_entry_ga_length(int index);

kis_link_EXPORT int iot_device_get_grouptable_entry_cflags(int index);

kis_link_EXPORT void iot_device_set_pm(bool programmingMode);

#endif