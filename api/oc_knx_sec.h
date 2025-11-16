/*
// Copyright (c) 2021-2023 Cascoda Ltd
// Copyright (c) 2024-2025 KNX Association
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
/**
	@brief knx application level security
	@file
*/

#ifndef OC_KNX_SEC_INTERNAL_H
#define OC_KNX_SEC_INTERNAL_H

#include "oc_ri.h"

#ifdef __cplusplus
extern "C" {
#endif

  #define DEFAULT_OSN_DELAY (1000)	// default (ms) defined by iot specification -> able to be modified by PUT

	/**
	 * @brief The token profiles
	 * see section 3.5.4.2 Access Token Resource Object
	 */
	typedef enum
	{
		OC_PROFILE_UNKNOWN = 0,         // unknown profile
		OC_PROFILE_COAP_DTLS = 1,       // "coap_dtls" 
		OC_PROFILE_COAP_OSCORE = 2,     // "coap_oscore" 
		OC_PROFILE_COAP_TLS = 254,      // "coap_tls" [OSCORE] for [X.509] certificates with TLS 
		OC_PROFILE_COAP_PASE = 255      // "coap_pase" [OSCORE] with PASE credentials
	} oc_at_profile_t;

	/**
	 * @brief access token profile to string
	 *
	 * @param at_profile the access token profile
	 * @return char* the string denoting the at access token profile
	 */
	char* oc_at_profile_to_string(oc_at_profile_t at_profile);

	/**
	 * @brief Access Token (at) Information
	 * payload for a unicast message
	 * Example(JSON):
	 * ```
	 *{
	 * "id": "OC5BLLhkAG ...",
	 * "profile": "coap_oscore",
	 * "scope": ["if.sec", "if.p"],
	 * "cnf": {
	 * "osc": {
	 * "alg": "AES-CCM-16-64-128", (decimal 10)
	 * "id": "<kid>/<sid>",
	 * "rid": "SID for response",
	 * "ms": "f9af8s.6bd94e6f"
	 * }}}
	 * ```
	 * second example of (JSON) payload for a group address:
	 * ```
	 *{
	 * "id": "OC5BLLhkAG ...",
	 * "profile": "coap_oscore",
	 * "scope": [0, 1, 2],
	 * "cnf": {
	 * "osc": {
	 * "alg": "AES-CCM-16-64-128",
	 * "id": "<kid>/<sid>",
	 * "ms": "f9af8s.6bd94e6f"
	 * }}}
	 * ```
	 * scope : "coap_oscore" [OSCORE] or "coap_dtls"
	 *
	 *  | name      | CBOR key | CBOR type  | mandatory  |
	 *  |-----------|----------|------------|------------|
	 *  | id        | 0        | string     | yes        |
	 *  | profile   | 38       | unsigned   | yes        |
	 *  | scope     | 9        | str/int [] | yes        |
	 *  | cnf       | 8        | map        | yes        |
	 *  | osc       | 4        | map        | oscore     |
	 *  | kid       | 2        | string     | optional   |
	 *  | nbf       | 5        | integer    | optional   |
	 *  | sub       | 2        | string     | conditional|
	 *
	 *
	 * Specific oscore values (ACE):
	 *
	 * https://datatracker.ietf.org/doc/html/draft-ietf-ace-oscore-profile-19#section-3.2.1
	 *
	 * | name      | CBOR label | CBOR type   | description                        |default value              |
	 * | ----------| -----------| ------------|------------------------------------|---------------------------|
	 * | id        | 0          | string      | full ctx identifier                | -                         |
	 * | ms        | 8:4:2      | byte string | Master Secret value (shall be PSK) | -                         |
	 * | version   | 8:4:1      | uint        | OSCORE Version                     | 1                         |
	 * | hkdf      | 8:4:3      | integer     | HKDF value                         | HKDF SHA-256  (-10)       |
	 * | alg       | 8:4:4      | integer     | AEAD Algorithm                     | AES-CCM-16-64-128 (10)    |
	 * | salt      | 8:4:5      | byte string | Master Salt                        | Default empty byte string |
	 * | contextId | 8:4:6      | byte string | OSCORE ID Context value            | omit                      |
	 * | osc_id    | 8:4:0      | byte string | OSCORE SID                         | -                         |
	 *
	 * Example payload:
	 * ```
	 * {
	 *   "alg" : "AES-CCM-16-64-128",
	 *   "id" : b64'AQ=='
	 *   "ms" : b64'+a+Dg2jjU+eIiOFCa9lObw'
	 * }
	 * ```
	 * @note
	 * - maps are not stored
	 *
	 */
	typedef struct oc_auth_at
	{
		oc_string_t id;							// (0) id, hex encoded 
		oc_acl_mask_t scope;	      // (9) acl scopes (compacted as bit field)
		oc_at_profile_t profile;		// (38) "coap_oscore" or "coap_dtls", only oscore implemented
		oc_string_t sub;						// (2) TLS kid (optional - not used)
		oc_string_t kid;						// (8:3) TLS cnf:sub (optional - not used)
		oc_string_t osc_version;		// (8:4:1) OSCORE cnf:osc:version (optional - not used) 
		oc_string_t osc_ms;					// (8:4:2) OSCORE cnf:osc:ms (byte string) 
		uint8_t osc_hkdf;						// (8:4:3) OSCORE cnf:osc:hkdf (optional - not used) default:	decimal value
		uint8_t osc_alg;						// (8:4:4) OSCORE cnf:osc:alg (optional - not used) default: decimal value 10
		oc_string_t osc_salt;				// (8:4:5) OSCORE cnf:osc:salt default: empty string 
		oc_string_t osc_contextid;	// (8:4:6) OSCORE cnf:osc:contextid -> 'kid_context' (in msg) / 'ID Context'  (OSC) / osc:contextid (OSC Profile) - max 16 byte string 
		oc_string_t osc_id;         // (8:4:0) OSCORE cnf:osc:id -> 'kid' (in msg) / 'Sender ID' (OSC) / osc:id (OSC Profile) - max 7 byte string 
		int nbf;										// token not valid before (optional - not used) 
		int ga_len;									// length of the group addresses (ga) in the scope, specification demands at least 20 entries must be supported
		uint32_t* ga;								// (777, artificial number) group address array of 32 bit values 

	} oc_auth_at_t;

	/**
	 * @brief returns the amount of total entries of the auth/at table
	 *
	 * @note
	 * - returned size depends on if AT table is present (>0) or not (=0)
	 * - defined as extra method, to be used from extern
	 *
	 * @return the allocated amount of entries of the auth/at table
	 */
	int oc_core_get_at_table_size(void);

	/**
   * @brief returns the amount of used entries of the auth/at table
   *
   * @return the allocated amount of entries of the auth/at table
   */
	int oc_core_items_used_in_auth_at_table(void);

	/**
   * @brief Find (all) PASE entries in the access token table and delete them 
	*	       - from RAM
	*				 - from storage (file system)
	*        - from possible context references
	* 
	* @note there should be only one entry, everything else is a stack (PASE handling) problem 
	*/
	void oc_core_find_and_remove_pase_token_in_at_table(void);

	/**
	 * @brief find empty slot
	 *
	 * @return int -1 : no space left
	 * @return int >=0 : index to place entry
	 */
	int oc_core_find_at_entry_empty_slot(void);

	/**
   * @brief set shared (SPAKE) key to the auth at table, on the device (server) side
   *
   * @param client_sender_id device id that has been negotiated with SPAKE2+, it will become the kid (Sender ID) within the OSCORE context. 
   * @param client_sender_id_size sender id size
   * @param shared_key the master key after SPAKE2 handshake
   * @param shared_key_size key size
   */
	void oc_oscore_set_auth_shared(char* client_sender_id, int client_sender_id_size, uint8_t* shared_key, int shared_key_size);

	/**
	 * @brief retrieve auth/at entry
	 *
	 * @param index the index in the table
	 * @return oc_auth_at_t* the auth at entry
	 */
	oc_auth_at_t* oc_get_auth_at_entry(int index);

	/**
	 * @brief print the AT table entry (debugging) if present (id > 0)
	 *
	 * @param index the index in the table to be printed
	 */
	void oc_print_auth_at_entry(int index);

	/**
	 * @brief deletes the entire AT table
	 * - from RAM
	 * - from storage (file system)
	 *
	 */
	void oc_delete_at_table(void);

	/**
	 * @brief deletes the entire AT table, except entries with scope = "if.sec"
	 * - from RAM
	 * - from storage (file system)
	 *
	 *@note will be used in reset of the device
	 */
	void oc_delete_at_table_except_sec_scope_entries(void);

	/**
	 * @brief deletes an AT table entry
	 * - from RAM
	 * - from storage (file system)
	 *
	 * @param entry the index in the table
	 * return 0 == success
	 */
	int oc_delete_at_table_entry(int entry);

	/**
	 * @brief Creation of the KNX security resources.
	 *
	 *
	 * creates the following resources:
	 * - /auth/o
	 * - /auth/o/rplwdo
	 * - /auth/o/osndelay
	 * - /auth
	 * optional:
	 * - a/sen
	 *
	 */
	void oc_create_knx_sec_resources(void);

	/**
	 * @brief initialize all OSCORE context from AT table content
	 *
	 * @note
	 * - OSCORE context entries are an internal linked list
	 * - called after device reset/ restart or on POST /auth/at table
	 * - is used to not issue after a reset/ restart for all new requests a 4.01 unauthorized cycle
	 *
	 * @param read_ssn_from_storage if content is read from storage (yes/no), this affects how to handle the SSN (true usually after device reset/ restart)
	 */
	void oc_init_oscore_from_storage(bool read_ssn_from_storage);

	/**
	 * @brief function to check if the at_interface is listed in the resource
	 * interfaces
	 *
	 * @param caller_scope interface to be checked
	 * @param called_scope interface to be matched (resource).
	 * @return true one of the scopes listed in resource acl list
	 * @return false none of the scopes listed in resource acl list
	 * @note done as an individual function to use in tests
	 */
	bool oc_knx_contains_interface(oc_interface_mask_t caller_scope,
																 oc_interface_mask_t called_scope);

	/**
	 * @brief check access control based on:
	 *        - acl scope (auth table)
	 *        - resource scope
	 *        - if the method is available for the resource
	 *
	 * @param method invocation method for this call
	 * @param resource the resource being called
	 * @param endpoint the endpoint that calls for an operation
	 * @param value_object the value object pointer, to check the group address in case of scope 'if.g.s'
	 *
	 * @note unsecured resources are always allowed
	 *
	 * @return true has access (the resource is unsecured/public or the ACL has a match)
	 * @return false does not have access
	 */
  bool oc_knx_sec_check_acl(oc_method_t method, const oc_resource_t* resource, oc_endpoint_t* endpoint, oc_rep_t* value_object);

	/**
	 * @brief returns AT entry with OSCORE ID
	 *
	 * @param osc_id OSCORE ID
	 * @param osc_id_len OSCORE ID length
	 *
	 * @return int -1 : AT entry not found
	 * @return int >=0 : index to place entry
	 */
  int oc_core_find_at_entry_with_osc_id(uint8_t* osc_id, size_t osc_id_len);

	/**
   * @brief get OSCORE Replay Window Size
   *
   * @return window size
   */
	uint32_t get_oscore_replay_window_size(void);

	/**
   * @brief get OSCORE OSN Delay Time
   *
   * @return delay time (ms)
   */
	uint32_t get_oscore_osn_delay_ms(void);

	/**
   * @brief set OSCORE OSN Delay Time
   *
   * @param milliseconds time
   *
   * @note don't allow window size > 64 (used window is of type uint64_t = 64 bits possible)
   *
   */
  void set_oscore_osn_delay_ms(uint16_t milliseconds);

#ifdef __cplusplus
}
#endif

#endif 
