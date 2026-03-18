/* 
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */
 
/*
 * Note
 * The file 'knx_iot_ems.c' is NOT a part of the stack or not intended to be an
 * 'application' library. It hosts only for the below described EMS application commonly used
 * functionality in one place.
 */

#ifndef KNX_IOT_EMS_H
#define KNX_IOT_EMS_H

#include "apps/knx_iot_app.h"

typedef enum cem_mode_t
{
  sun_mode = 0,
  mix_mode = 1
} cem_mode_t;

#define CEM_INVERTER (0)
#define CEM_CHARGER (1)
#define CEM_INVERTER_TRESHOLD (3990) // float compare is tricky, so make it easy

#ifdef __cplusplus
extern "C"
{
#endif

  cem_mode_t get_cem_mode(void);
  void set_cem_mode(cem_mode_t mode);

  /* EMS KNX-IoT app interface functions */

  // CEM KNX-IoT app interface functions
  float get_cem_inverter_value(void);
  float get_cem_charger_value(void);
  char* get_cem_inverter_href(void);
  uint8_t get_cem_inverter_flags(void);
  void clear_cem_inverter_flags(uint8_t flags);

  void set_cem_charger_value(float value);
  char* get_cem_charger_href(void);
  uint8_t get_cem_charger_flags(void);
  void clear_cem_charger_flags(uint8_t flags);

  // inverter KNX-IoT app interface functions
  void set_inverter_value(float value);
  char* get_inverter_href(void);
  uint8_t get_inverter_flags(void);
  void clear_inverter_flags(uint8_t flags);

  // charger KNX-IoT app interface functions
  float get_charger_value(void);
  char* get_charger_href(void);
  uint8_t get_charger_flags(void);
  void clear_charger_flags(uint8_t flags);

#ifdef __cplusplus
}
#endif
#endif
