/* 
 * Copyright (c) 2022-2023 Cascoda Ltd
 * Copyright (c) 2024-2026 KNX Association
 *            
 * SPDX-License-Identifier: Apache-2.0
 */     

/*
 * Note that the file 'knx_iot_app.c/h' is NOT a part of the stack or not intended to be an 
 * 'application' library. It hosts only for the application demos commonly used functionality in one place.
 */

#ifndef KNX_IOT_APP_H
#define KNX_IOT_APP_H
#include "oc_api.h"
#include "oc_helpers.h"

// use it in upper case (min 6, max 32), IMPORTANT consider the notes for the PASE Resource Object (oc_pase_t)
#define PASSWORD "2X4W3TE0DFLLS19Y1FCH"	// TODO FIXME move/handle this somewhere/somehow better

// Callback Notes
/*
 GET/PUT application callback handlers are defined to access the data point resources.

 Note that the handlers are a collection of - by the stack demo - used PUT/GET methods.
 Moreover, a generic (GET) handler is used, to allow a channel based approach with one handler.

 For an own development the methods have to be adapted or extended, such as to define get/put
 methods for float,long int or combined datapoints, or to handle metadata parameters on PUT.

 For the resource path, resource types and other see 'register resources'.
 A callback 'call' handler demands the below defined 3 parameters when called by the stack,
 provide them even if they are not used.

 @param request    the request representation
 @param interfaces the interface mask, as specified for the application resource and method (GET, ...)
 @param user_data  the user data, can be freely used such as to address several resources with one handler
                   (see Datapoint Notes below)

 Details
 -------

 *Caller*

 The callbacks are handled from the stack as a:
 - group communication 's-mode' call (a mc/uc POST to /k)
 - parameter and diagnostic 'property' call (an uc POST to /p or an uc PUT/GET to /p/{property-path})

 Note that a POST to /k,/p is forward by the stack always to the callback PUT handler.

 For a 'property' call
 - the corresponding application GET/PUT application callback handlers are called
   by the stack (group object table things are NOT considered).
 - the request payload points to the actual value object (for PUT)

 For an 's-mode' call
 - the group object table configuration flags (cflags) and service type (w/r/a)
   are considered by the stack.
 - the corresponding application GET(r)/PUT(w/a) application callback handlers are called
   by the stack.
 - the request payload points to the actual value object (for PUT(w/a))

 *Resource Path*

 - A KNX related resource path for the 's-mode' and 'property' calls SHALL be defined with
   a leading '/p' (e.g.; '/p/lssb/soo'). The resource path SHALL NOT be empty. Hence, the stack
   application examples uses the leading '/p' with some application specific extension,
   also the EITT test application requires a leading '/p' for the EITT certification tests.

 - All /p callbacks MUST implement also additional required functionality.

     - GET is mandatory for 's-mode' and 'property' calls
     - PUT is optional** for 's-mode' calls and 'property' calls

     **Depending on your application and hardware you may (not) allow to write (PUT) values to an
       output datapoint (GO), this can damage your hardware. Reading an input datapoint is less
       critical, but requires a kind of caching the value. An EXAMPLE how to handle/distinguish
       the 's-mode' and 'property' calls and options how to react is given below in the callback handler code.
       Another option to circumvent the problem is to not declare the PUT handler for those resources (GOs) where
       a PUT is not possible.

     - Read metadata by using a GET + query (?m=m/o parameters) is mandatory
       - (m) mandatory parameters (id, value, rt, if, dpt, ga, href)
       - (o) optional parameters (desc, unit, min, max, mrt, cov, hbt, sns)

     - Write metadata by using a PUT + query (?m=m/o parameters) is optional, this adheres to the
       POST /p + query (?m=m/o parameters) -> if PUT can do that POST must also allow that (and vice versa).
       - (m) mandatory parameters (id, value, rt, if, dpt, ga, href)
       - (o) optional parameters (desc, unit, min, max, mrt, cov, hbt, sns)

 - Outputs with interface type if.o MUST support OBSERVE

 - A NON KNX related resource path can be defined for any vendor specific (configuration) purpose. In this case
   the device configuration is also vendor specific, e.g; by a vendor client. It MAY also be supported in the future by
   a KNX MaC's, such as via an extension of the product SDK.

*/

// Datapoint Notes
/*
 Datapoint definitions are used to register/create a datapoint resource in the application. 
 Either the href/description/... data consumes the space in a static structure definition as below,
 or they are hard coded when you register them, so no space difference, but better structured.

  - value types must respect the bit size definition of a MaC (ETS) product, e.g.; 32-bit int or bool

  - the resource path, details see callback handler 'Callback Notes'

  - the resource (DPA) type MUST be in FULL URN notation:
    - a GET {ipv6-unicast}/{point-path}?m asks with SHORT URN (see handler)
    - a GET {ipv6-multicast}/.well-known/core asks with SHORT URN or FULL URN
    - scanning all application resources demands a FULL URN

  - the resource (DPT) type
  
  - the 'id' is optional and can be added/removed (to reduce resource consumption),
    if included as part of a point it can be used for an n-fold channel oriented application
    to define a generic PUT/GET handler for all channels. The addressed channel and datapoint can be identified
    from the generic handler, e.g. by setting the value to ch# << 8 + point# (see application handler examples).

  - the 'flags' is optional and can be added/removed (to reduce resource consumption),
    if included as part of a point it can be used to inform an upper layer (such as a C++ GUI application
    on caller actions and errors)   

*/

/**
 * @brief handler flags (bit map), 
 * these flags are used to determine what to do on application level
 */
typedef enum
{
  DPH_NO_ERROR = 0,         // no error 
  DPH_ERROR = 1,            // handler error occurred
  DPH_GET = 2,              // was a get, request contains it, but on upper layer it is not present anymore 
  DPH_PUT = 4,              // was a put, request contains it, but on upper layer it is not present anymore 
  DPH_NEW_EVENT = 8         // new event occured, should be reset if event was processed in upper layer
} app_datapoint_handler_flags_t;

/* Basic Datapoints */
typedef struct
{
  volatile bool value;  // the actual datapoint type, see notes above
  char* resource_path;  // the resource path such as /p/...
  char* dpa;            // annotated datapoint, see in KNX ioT specification 3/10/5 
  char* dpt;            // datapoint type, see in KNX ioT specification 3/10/5
  uint16_t id;          // see note above
  char* name;
  volatile app_datapoint_handler_flags_t flags;
} bool_datapoint_t;

typedef struct
{
  volatile float value;
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
  volatile app_datapoint_handler_flags_t flags;
} float_datapoint_t;

typedef struct
{
  volatile int value; 
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
  volatile app_datapoint_handler_flags_t flags;
} int_datapoint_t;

/* Reduced Datapoints for Examples */
typedef struct
{
  volatile bool value;  // the actual datapoint type, see notes above
  char* resource_path;  // the resource path such as /p/...
  char* dpa;            // annotated datapoint, see in KNX ioT specification 3/10/5 
  char* dpt;            // datapoint type, see in KNX ioT specification 3/10/5
  uint16_t id;          // see note above
} bool_datapoint_no_name_no_flags_t;

typedef struct
{
  volatile int value; 
  char* resource_path;
  char* dpa;
  char* dpt;
  char* name;
} int_datapoint_no_flags_t;

// Functional Block Notes
/*
  An FB consists of a number of datapoints with its values, endpoints (EP) and URNs.

  - the FB number, despite any DPA scheme that is used from the in FB included datapoints
    (note that an FB such as 417 may also reuse predefined datapoints from other FB's with DPA type 312.xx, 2nn.xx
     or similar, FB 421 is NOT only using 'self defined' 417.xx types)

  - the FB instance, 0...n, 0 = only one instance, > 0 more than one instance, see also 'oc_resource_set_function_block_data'

  - the number of 'visible' datapoint in an FB, note that if this number MAY change e.g.; when adding/deleting resources
    or make some invisible
    - caused by ETS (e.g, partial download with changed parameter setting)
    - caused by own application at runtime (e.g, HMI parameter adjustment by user)
    the correct number must be re-applied by the application to the FB.

  - the datapoints, see above

 A FB can be also defined on channel oriented structure, such as used for the LSAB/LSSB demos.

*/

#define NUM_CHANNELS (2)    // common data for LSAB/LSSB/EITT
#define NUM_CEM_POINTS (2)  // common data for CEM

/* Basic Function Blocks */
typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  int_datapoint_t point;
} int_functional_block_t; // see Functional Block Notes

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  float_datapoint_t point;
} float_functional_block_t; // see Functional Block Notes

/* Reduced and Special Function Blocks for Examples */
typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  int_datapoint_no_flags_t point; // reduced int datapoint for example
} int_functional_block_no_flags_t; // see Functional Block Notes

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  float_datapoint_t point[NUM_CEM_POINTS];
} float_array_functional_block_t; // see Functional Block Notes

typedef struct
{
  uint16_t fb_number;
  uint8_t fb_instance;
  uint8_t fb_number_of_datapoints;
  bool_datapoint_no_name_no_flags_t point[NUM_CHANNELS];  // minimal bool datapoint type for example
} bool_array_functional_block_no_name_no_flags_t, lsxb_channel_t; // see Functional Block Notes

// Application callback for PUT
// Note:
// No need for flags as we know when this is called everything went well and
// it's DPH_NO_ERROR + DPH_PUT + DPH_NEW_EVENT
typedef void (*knx_iot_put_callback_t)(void* user_data, const oc_rep_value_type_t value_type, volatile void* value, const char* description);

#ifdef _WIN32
#include <direct.h>
#define GetCurrentDir _getcwd                  // path of current working directory, WIN
#elif defined(__linux__) || defined(__APPLE__) // linux, mac specific code
#include <unistd.h>
#define GetCurrentDir getcwd                   // path of current working directory, linux, mac
#endif

/*
  Definition of weak symbol for cross-platform compatibility.
  - on Windows with MSVC the weak symbol is not supported for functions, hence we define an empty macro for now
  - TODO find a better way to handle weak symbols cross-platform
 
 */
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#define KNX_TOOL_WEAK
#elif defined(__GNUC__)
#define KNX_TOOL_WEAK __attribute__((weak))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Function to set up the device on stack startup.
   *        - sn, application name, hwv, hwt, device model -> permanent
   *        - fwv, application version, hostname -> volatile, may be changed by MaC configuration
   *
   * @note
   * - It initializes data with permanent values,
   *   but also data with their (volatile) default values that may be changed or where
   *   already changed at runtime (see above).
   * - It is called at the end of 'knx_iot_initialize_stack', before reading device storage data.
   *   Hence, written default values on 'init' may be overwritten (again) with storage values
   *   such as the storage hostname (if present on storage, then this would be correct).
   *     
   */
  int app_init(void);
  
  /**
   * @brief initialize the stack
   *
   * @param storage_name the storage name, max 64 chars, more chars are cut
   *
   * @note the storage name will be appended by the device serial number,
   *       the storage as such is used to save the device configuration data,
   *       the data is stored in the current directory
   *       for Windows or Linux the storage name is the folder name
   *                            
   * @return int 0 == success
   */
  int knx_iot_initialize_stack(const char* storage_name);
  
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
  void knx_iot_factory_presets_cb(void* data);
 
  /**
   * @brief
   * Application restart callback handler for the device
   * @param data the supplied data.
   */
  void knx_iot_restart_cb(void* data);

  /**
   * @brief initializes the global variables
   * for the resources
   * for the parameters
   */
  void knx_iot_initialize_variables(void);
  
  /**
   * @brief
   * Application set hostname callback handler for the device
   *
   * @param hostname the new hostname of the device to be maintained.
   * @param data the supplied data.
   */
  void knx_iot_set_hostname_cb(const oc_string_t hostname, void* data);
  
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
  void swu_cb(oc_separate_response_t* response, size_t binary_size, size_t block_offset, const uint8_t* block_data, size_t block_len, void* data);
  
  /**
   * @brief software update upgrade trigger callback
   * 
   * Called when /swu/update receives a PUT request, indicating the device should
   * start the firmware upgrade process
   * 
   * @param defer_time requested defer time in seconds before starting upgrade
   * @param data user data
   */
  void swu_upgrade_cb(int defer_time, void* data);
  
  /**
   * @brief add all short interface urn's to the 'root' object with string key 'if'
   *
   * @param resource the resource
  
   */
  void add_all_interface_short_urns_for_a_resource(const oc_resource_t* resource);
  
  /**
   * @brief s-mode response callback,
   *        will be called when a response is received on an s-mode read request
   *
   * @param url the url
   * @param rep the full response
   * @param rep_value the parsed value of the response
   */
  void oc_s_mode_response_cb(char* url, oc_rep_t* rep, oc_rep_t* rep_value);
  
  // TODO add documentation
  void knx_iot_signal_event_loop(void);

  // TODO add documentation
  int knx_iot_initialize_app(void);
 
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
   *    slice (period is 1 second) with oc_resource_set_periodic_observable(res_InfoOnOff_?, 1).
   *    Set observable events are send when oc_notify_observers(oc_resource_t *resource) is called.
   *    This function must be called when the value changes, preferable on an interrupt when
   *    something is read from the hardware.
   */
  void knx_iot_register_resources(void);

  /**
   * @brief Generic GET request handler for KNX IoT datapoints.
   *
   * Processes incoming GET requests for datapoint resources. Handles content
   * negotiation, metadata queries, and value retrieval. Formats the response
   * as CBOR-encoded data according to the specified value type.
   *
   * @param request Pointer to the OC request structure containing request details
   * @param interfaces Interface mask indicating which interfaces are being accessed
   * @param user_data User-supplied data pointer (e.g. encoding channel/datapoint identifiers)
   * @param value_type The OC representation value type (e.g. OC_REP_BOOL, OC_REP_INT, OC_REP_FLOAT)
   * @param value Pointer to the datapoint value to be returned in the response
   * @param flags Pointer to handler flags indicating request type and error state
   * @param description Human-readable description of the datapoint (for metadata)
   *
   * @note A GET request also handles metadata queries regardless of whether the
   *       datapoint is input or output  // TODO also output?
   * @note The request must have valid accept header (APPLICATION_CBOR); returns
   *       error response if content type is unsupported
   */
  void knx_iot_get_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data,
          const oc_rep_value_type_t value_type, volatile void* value, 
          volatile app_datapoint_handler_flags_t* flags, char* description);
  
  /**
   * @brief Generic PUT request handler for KNX IoT datapoints.
   *
   * Processes incoming PUT requests for datapoint resources. Validates the incoming
   * value against the specified type, updates the datapoint value, and invokes an
   * optional callback for application-specific post-update handling (e.g., hardware
   * control, status synchronization).
   *
   * @param request Pointer to the OC request structure containing request details and payload
   * @param interfaces Interface mask indicating which interfaces are being accessed
   * @param user_data User-supplied data pointer (e.g. encoding channel/datapoint identifiers)
   * @param value_type The OC representation value type (e.g. OC_REP_BOOL, OC_REP_INT, OC_REP_FLOAT)
   * @param value Pointer to the datapoint value variable to be updated with the request payload
   * @param callback Optional callback function invoked after value update (may be NULL)
   * @param flags Pointer to handler flags indicating request type and error state
   * @param description Human-readable description of the datapoint (for metadata)
   *
   * @note The request payload must match the specified value_type; type mismatches result in error responses
   * @note If callback is NULL, no post-update action is performed
   */
  void knx_iot_put_handler(oc_request_t* request, oc_interface_mask_t interfaces, void* user_data,
          const oc_rep_value_type_t value_type, volatile void* value, knx_iot_put_callback_t callback,
          volatile app_datapoint_handler_flags_t* flags, const char* description);
  
#ifdef __cplusplus
}
#endif

#endif
