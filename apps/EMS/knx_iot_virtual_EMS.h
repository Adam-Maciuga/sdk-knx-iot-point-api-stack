/*
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Copyright (c) 2022-2023 Cascoda Ltd
 Copyright (c) 2024-2025 KNX Association
-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/




#ifndef KNX_IOT_VIRTUAL_EMS_H
#define KNX_IOT_VIRTUAL_EMS_H


typedef struct
{
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
  char* id; // used to identify for a generic PUT/GET handler the channel number/ datapoint number
} datapoint_t;


#ifdef __cplusplus
extern "C"
{
#endif   

  const int SUN_MODE = 0;
  const int MIX_MODE = 1;

  // PV app
  int PV_init_auth_table();
  int PV_init_tables_QR(char*);
  char* PV_retrieve_href(uint16_t);
  void PV_set_PV(int);  
  void PV_get_PV(oc_request_t*, oc_interface_mask_t, void*);

  // CEM app
  int CEM_init_auth_table();
  int CEM_init_tables(char*);
  char* CEM_retrieve_href(uint16_t);
  int CEM_retrieve_mode();
  int CEM_retrieve_pv();
  int CEM_retrieve_charger();
  void CEM_process_pv();
  void CEM_set_mode(int);
  void CEM_set_charger(int);
  void CEM_set_link(uint64_t);
  void CEM_put_PV(oc_request_t*, oc_interface_mask_t, void*);
  void CEM_get_charger(oc_request_t*, oc_interface_mask_t, void*);  
  void CEM_get_link(oc_request_t*, oc_interface_mask_t, void*);  

  // Charger app
  int Charger_init_auth_table();
  int Charger_init_tables_QR(char*);
  int Charger_retrieve_charger();
  void Charger_put_charger(oc_request_t*, oc_interface_mask_t, void*);

#ifdef __cplusplus
}
#endif
#endif
