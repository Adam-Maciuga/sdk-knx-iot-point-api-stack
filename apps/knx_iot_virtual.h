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

// include file, used for all GUI applications (EITT/LSAB/LSSB)


#ifndef KNX_IOT_VIRTUAL_H
#define KNX_IOT_VIRTUAL_H

// network

/*
 The network router may not allow to send multicast with scope 5 (site local),
 hence the DEMO applications use scope 2 instead. If needed,
 sendout with scope 2 and 5 separately may be an option (2 messages). 
 */
#define SENDER_SCOPE (2)

// common data
#define NUM_CHANNELS (2)
#define NUM_POINTS   (2)
#define SOO  (0)
#define IOO  (1)


// use it in upper case (min 6, max 32)
// IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"

/*

 Datapoint definitions, used to register/create a datapoint resource in the application, 
 either the href/description/... data consumes the space this static structure definition, or
 they are hard coded when you register them, so no space difference but better structured


  - value types must respect the bit size definition of a MaC (ETS) product, e.g.; 32-bit int or bool

  - the resource path, details see callback handler 'Callback Notes'

  - the resource (DPA) type MUST be in FULL URN notation:
    - a GET {ipv6-unicast}/{point-path}?m asks with SHORT URN (see handler)
    - a GET {ipv6-multicast}/.well-known/core asks with SHORT URN or FULL URN
    - scanning all application resources demands a FULL URN

  - the resource (DPT) type
  

  - the id used for an n-fold channel oriented application to define a generic PUT/GET handler for all channels,
    the addressed channel and datapoint can be identified from the generic handler, e.g. by setting the value
    to ch# << 8 + point# (see code application examples)

*/
typedef struct
{
  volatile bool value; // the actual datapoint type, see notes above
  char* resource_path; // the resource path such as /p/...
  char* dpa;    // annotated datapoint, see in KNX ioT specification 3/10/5 
  char* dpt;    // datapoint type, see in KNX ioT specification 3/10/5
  uint16_t id;  // see note above
} bool_datapoint_t;


typedef struct
{
  volatile unsigned int value; 
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
} int_datapoint_t;

/*
  Defines the basic (channel oriented) structure of a LSAB/LSSB/EITT functional block definition.

  An FB consists of a number of datapoints with its values, endpoints (EP) and URNs.

  - the FB number, despite any DPA scheme that is used from the in FB included datapoints
    (note that an FB such as 417 may also reuse predefined datapoints from other FB's with DPA type 312.xx, 2nn.xx or similar, 
     FB 421 is NOT only using 'self defined' 417.xx types)

  - the FB instance, 0...n, 0 = only one instance, > 0 more than one instance, see also 'oc_resource_set_function_block_instance'

  - the number of 'visible' datapoint in an FB, note that if this number WOULD change e.g.; when adding/deleting resources or make some invisible   
    - caused by ETS (e.g, partial download with changed parameter setting)
    - caused by own application at runtime (e.g, HMI parameter adjustment by user)
    the correct number must be applied by the application to the FB. 

  - the datapoints, see above

*/
typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;

  bool_datapoint_t point[NUM_POINTS];
} lsxb_channel_t, functional_block_t;


#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Function to set up the device on stack startup.
   *        It is called at the end of 'app_initialize_stack'
   */
  int app_init(void);

  /**
   * @brief initialize the stack
   *
   * @return int 0 == success
   */
  int app_initialize_stack(void);
  
  /**
   * @brief Set a bool
   *
   * @param channel the channel for the bool to set
   * @param point the point of the channel for the bool to set
   * @param value value to set
   */
  void app_set_bool_variable_from_channel(uint8_t channel, uint8_t point, bool value);

  /**
   * @brief Get a bool
   *
   * @param channel the channel for the bool to get
   * @param point the point of the channel for the bool to get
   */
  bool app_retrieve_bool_variable_from_channel(uint8_t channel, uint8_t point);

  /**
   * @brief Get a URL
   *
   * @param channel the channel for the URL to get
   * @param point the point of the channel for the URL to get
   * @return boolean variable
   */
  char* app_retrieve_href_from_channel(uint8_t channel, uint8_t point);

  /**
   * @brief retrieves the url of a parameter
   * index starts at 1
   * @param index the index to retrieve the url from
   * @return the url or NULL
   */
  char* app_get_parameter_url(int index);

  /**
   * @brief retrieves the name of a parameter
   * index starts at 1
   * @param index the index to retrieve the parameter name from
   * @return the name or NULL
   */
  char* app_get_parameter_name(int index);

  /**
   * @brief returns the SPAKE2+ client password, used from external application hence defined as
   *        separate method.
   *
   * @note  IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
   */
  char* app_get_password(void);

  /**
 * @brief
 * Application factory preset callback handler for the device
 * @param data the supplied data.
 */
  void factory_presets_cb(void* data);

  /**
   * @brief initializes the global variables
   * for the resources
   * for the parameters
   */
  void initialize_variables(void);

  /**
   * @brief
   * Application host name callback handler for the device
   *
   * @param host_name the host name of the device to be maintained (check/set,
   * print, ...)
   * @param data the supplied data.
   */
  void hostname_cb(const oc_string_t host_name, void* data);

  /**
   * @brief function to set the input string to upper case
   *
   * @note extra function defined, since '_strupr' from <string.h> is Microsoft (Windows) 
           specific and not available in Linux in <string.h>
   *
   * @param str the string to make upper case
   *
   */
  void app_str_to_upper(char* str);


  /**
   * @brief software update callback
   *
   * @param response the instance of an internal struct that is used to track the state of the separate response
   * @param binary_size the full size of the binary
   * @param block_offset the offset of the image
   * @param block_data the image data
   * @param block_len the length of the image data
   * @param data the user data
   */
  void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t block_offset, uint8_t* block_data, size_t block_len, void* data);

  /**
   * @brief add all short interface urn's to the 'root' object with string key 'if'
   *
   * @param resource the resource

   */
  void add_all_interface_short_urns_for_a_resource(const oc_resource_t* resource);

  /**
   * @brief s-mode response callback
   * will be called when a response is received on an s-mode read request
   *
   * @param url the url
   * @param rep the full response
   * @param rep_value the parsed value of the response
   */
  void oc_s_mode_response_cb(char* url, oc_rep_t* rep, oc_rep_t* rep_value);

  void get_lsxb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_lsab(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_lssb(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  void get_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);
  void put_test_parameter(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data);

  // need to define prototype, used by an init method
  void signal_event_loop(void);

  /**
   * @brief Register all the data point resources to the stack.
   * 
   * Each resource path is bind to a specific function for the supported methods:
   *   - GET (called from /p and /k)
   *   - PUT (called from /p and /k)
   *   - POST/DELETE/FETCH  (not supported from stack for the application)
   *
   * Each resource is:
   *   - secure
   *   - observable
   *   - discoverable through well-known/core
   *   - used interfaces as dpa.x.y (x : function block number, y : data point number)
   *
   * @note
   *	Periodic observable to be used when one wants to send an event per time
      slice (period is 1 second) with oc_resource_set_periodic_observable(res_InfoOnOff_?, 1).
      Set observable events are send when oc_notify_observers(oc_resource_t *resource) is called.
      This function must be called when the value changes, preferable on an interrupt when
      something is read from the hardware.
 */
  void register_resources(void);

#ifdef __cplusplus
}
#endif
#endif
