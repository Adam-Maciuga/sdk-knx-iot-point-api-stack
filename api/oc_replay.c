/*
// Copyright (c) 2023 Cascoda Ltd
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
#include <stdbool.h>
#include <inttypes.h>
#include "oc_replay.h"
#include "port/oc_clock.h"
#include "oc_config.h"
#include "messaging/coap/constants.h"
#include "oc_api.h"
#include "oc_knx_sec.h"

#ifndef OC_MAX_REPLAY_RECORDS
#define OC_MAX_REPLAY_RECORDS (20) 
#endif

#ifndef OC_MAX_MESSAGE_RECORDS
#define OC_MAX_MESSAGE_RECORDS (2)
#endif

#ifndef OC_REPLAY_RECORD_TIMEOUT
#define OC_REPLAY_RECORD_TIMEOUT (5)
#endif

static struct oc_replay_record
{
	uint64_t rx_ssn;        // most recent received client SSN
	oc_string_t rx_kid;     // byte string holding the client KID
	oc_string_t rx_kid_ctx; // byte string holding the client KID context (can be null)
	oc_clock_time_t time;   // time of last received client packet
	uint64_t window;        // bitfield indicating received client SSNs through bit position, 32 = default by OSCORE RFC (max 64 possible)
	bool in_use;            // whether this structure is in use & has valid data
} replay_records[OC_MAX_REPLAY_RECORDS] = { 0 };

// used to cache a message for a replay attack 
static struct oc_cached_message_record
{
  struct oc_message_s* message;		// pointer to message
	uint8_t token[COAP_TOKEN_LEN];	// used msg token 
	uint16_t token_len;
} message_records[OC_MAX_MESSAGE_RECORDS] = { 0 };

// make record available for reuse
static void free_record(struct oc_replay_record* rec)
{
	// bounds check, use of C pointer arithmetics
	if (replay_records <= rec && rec < replay_records + OC_MAX_REPLAY_RECORDS)
	{
		rec->rx_ssn = 0;
		rec->window = 0;
		oc_free_string(&rec->rx_kid);
		oc_free_string(&rec->rx_kid_ctx);
		rec->time = 0;
		rec->in_use = false;
	}
}

// find empty record in queue, if queue is full ... free oldest record
static struct oc_replay_record* get_empty_record(void)
{
	for (int i = 0; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		if (!replay_records[i].in_use)
			return replay_records + i;
	}
	// nothing free, free oldest record 

	// defines oldest record as first array element
	struct oc_replay_record* oldest_rec = replay_records;

	// finding of oldest record, to release it (on heap limitations) 
	for (int i = 1; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		if (replay_records[i].time < oldest_rec->time)
			oldest_rec = replay_records + i;
	}

	free_record(oldest_rec);
	return oldest_rec;
}

// find record with 'kid' and 'kid context'
static struct oc_replay_record* get_record(const oc_string_t rx_kid, const oc_string_t rx_kid_ctx)
{
	if (oc_byte_string_len(rx_kid) == 0)
	{
		return NULL; // rx kid not present -> a match is not applicable
	}

	for (int i = 0; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		// c pointer arithmetics
		struct oc_replay_record* rec = replay_records + i;

		if (rec->in_use)
		{
			bool rx_kid_match = oc_byte_string_cmp(rx_kid, rec->rx_kid) == 0;
			bool null_kid_context_match = oc_byte_string_len(rx_kid_ctx) == 0 && oc_byte_string_len(rec->rx_kid_ctx) == 0;
			bool kid_context_match = oc_byte_string_cmp(rx_kid_ctx, rec->rx_kid_ctx) == 0;

			// kid match and (kid context both '0' = response or both match = request)
			if (rx_kid_match && (null_kid_context_match || kid_context_match))
				return rec;
		}
	}
	return NULL;  // nothing found
}

replay_state_t oc_replay_check_client(uint64_t rx_ssn, oc_string_t rx_kid, oc_string_t rx_kid_ctx)
{
	/*
	With CoAP over UDP, you cannot guarantee messages are received in order,
	hence SSN needs to be checked against replayed SSNs.

	We can use the default anti-replay algorithm specified by OSCORE, which
	uses a sliding window in order to track every received SSN within a
	given range.

	This can be implemented very efficiently using a bitfield, where the
	position of bits indicate the SSN being considered, and the value
	of the bit indicates whether the packet has been received before

	The entire bitfield is LEFT shifted whenever the recorded SSN increases,
	thus 'sliding' the (left) window in a very efficient manner. Here's an example
	of the algorithm in operation, with a REDUCED 8-bit bitfield for readability:

	                                   1234 5678
	max received ssn = 8, bitfield = 0b0000'0001
	rcv ssn 6, 8 - 6 = bit 2, inside window + free -> accept delayed msg, set bit 2 
	:
  max received ssn = 8, bitfield = 0b0000'0101
  rcv ssn 0, 8 - 0 = bit 8, left side window -> send echo for unknown msg 
	:
	:
	max received ssn = 8, bitfield = 0b0000'0101
  rcv ssn 8, 8 - 8 = bit 0, inside window + blocked -> throw replayed msg with ssn 8
	:
	:
	max received ssn = 8, bitfield = 0b0000'0101
	rcv ssn 9, 8 - 9 = bit -1, right side window -> accept fresh msg, update ssn, 1 x l-shift bitfield, set bit 0
	:
	:                                  2345 6789
	max received ssn = 9, bitfield = 0b0000'1011
	rcv ssn 99, 9 - 99 = bit -90, right side window -> accept fresh msg, update ssn, 90 x l-shift bitfield, set bit 0
	:
	:                                           99
	max received ssn = 99, bitfield = 0b0000'0001
	:
	*/

	struct oc_replay_record* rec = get_record(rx_kid, rx_kid_ctx);

	if (!rec)
	{
		// no replay window record available, force echo option
		// regardless unicast/multicast, either on first pub message
		// or after a release of an old recipient context
		return ECHO;
	}

	// received message matched existing record, so this record is useful &
	// should be kept around - update the time (to prevent a release from heap)  
	rec->time = oc_clock_time();

	// rx_ssn (ssn from received message) = max value used is 32 bit, hence unproblematic
    const int64_t ssn_diff = (int64_t)(rec->rx_ssn - rx_ssn);
    const uint32_t replay_window_size = get_oscore_replay_window_size();

	PRINT("new ssn  = %" PRIu64, rx_ssn);            // 64 bit uint
	PRINT("old ssn  = %" PRIu64, rec->rx_ssn);       // 64 bit uint
	PRINT("ssn_diff = %" PRIi64, ssn_diff);          // 64 bit int
	PRINT("kid      = %s", oc_string(rx_kid));       // %s = string
	PRINT("wnd old  = %X" , (uint32_t)rec->window);  // 64 bit bit field

	if (ssn_diff >= 0)
	{
    // received SSN <= max value of received SSN , either received SSN is
    // - in window 
    // - out of left bound
		// diff >= window size -> out of left window bound
		// example: diff from 0...31 = is in 32 bit window ; diff >= 32 is on left side window  
		if (ssn_diff >= replay_window_size)
		{
            PRINT("wnd new  = %" PRIu64, rec->window); // 64 bit uint
		    PRINT("outside window (size %" PRIu32 ") left bound by %" PRIi64, replay_window_size, ssn_diff);
			return ECHO; // not known if it was (ever) received before  
		}

		// diff < size -> inside window
		// see if it has been received before, so this can be a replay
		if (rec->window & 1 << ssn_diff)
		{
            PRINT("wnd new  = %X", (uint32_t)rec->window); // %llu = 64 bit ulong bit field 
		    PRINT("inside window (size %" PRIu32 "), replay msg, window bit %" PRIi64 " (%X) is already ticked", replay_window_size, ssn_diff, (uint32_t)(1 << ssn_diff));
			return REPLAY; // known that it was received before 
		}

		// SSN not received before, tick that this SSN is now occupied
		// DO NOT remember SSN, it was not the max value of received SSN's
		rec->window |= 1 << ssn_diff;

		PRINT("wnd new  = %X", (uint32_t)rec->window); // %llu = 64 bit ulong bit field 
		PRINT("inside window (size %" PRIu32 "), new msg, tick window bit %" PRIi64 " (%X)", replay_window_size, ssn_diff, (uint32_t)(1 << ssn_diff));
		return SYNCED;
	}

	// received SSN > max value of received SSN -> fresh message, slide the window and accept the packet
	// note that shifting by an amount larger than the size of the type
	// is undefined behaviour, so we must zero the window manually here
	
	if (-ssn_diff >= replay_window_size)
    // 1 << 32++ = undefined for a 32 -bit value
		rec->window = 0;            
	else
    // 1 << 31 = ok for a 32-bit value = 10000000'..'..'00000000'
		rec->window <<= -ssn_diff;  

	// set bit 0, indicating ssn 'rec->rx_ssn' has been received
	// DO remember SSN, it is now the max value of received SSN's
	rec->window |= 1;
	rec->rx_ssn = rx_ssn;

	PRINT("wnd new  = %X", (uint32_t)rec->window); // %llu = 64 bit ulong bit field 
    PRINT("outside window (size %" PRIu32 ") right bound by " PRIi64, replay_window_size, -ssn_diff);
	return SYNCED;
}

void oc_replay_add_client(const uint64_t rx_ssn, const oc_string_t rx_kid, const oc_string_t rx_kid_ctx)
{
	struct oc_replay_record* rec = get_record(rx_kid, rx_kid_ctx);

	if (!rec)
	{ // no match on first try - get a free record
		rec = get_empty_record();
		oc_byte_string_copy(&rec->rx_kid, rx_kid);
		oc_byte_string_copy(&rec->rx_kid_ctx, rx_kid_ctx);
		rec->in_use = true;
	}

	/* 
	 - reinit record with fresh SSN + window (and tick the new ssn as already used),
	 - the option to receive possible older SSNs within the present/old window (if it was present and not a free record) is gone
    */
	rec->rx_ssn = rx_ssn;
	rec->window = 1;
	rec->time = oc_clock_time();
}

struct oc_message_s* oc_replay_find_msg_by_token(const uint8_t* token, const uint16_t token_len)
{
	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; i++)
	{
		if (!message_records[i].message)
			continue;

		if (message_records[i].token_len == token_len)
		{
			if (memcmp(token, message_records[i].token, token_len) == 0)
				return message_records[i].message;
		}
	}
	return NULL;
}

static struct oc_cached_message_record* find_record_by_msg(struct oc_message_s* msg)
{
	if (!msg)
		return NULL;

	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; i++)
		if (message_records[i].message == msg)
			return message_records + i;
	return NULL;
}

static struct oc_cached_message_record* find_empty_record(void)
{
	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; i++)
		if (message_records[i].message == NULL)
			return message_records + i;
	return NULL;
}

static oc_event_callback_retval_t oc_replay_free_msg_handler(void* msg)
{
	struct oc_cached_message_record* rec = find_record_by_msg(msg);
	if (rec)
	{
		rec->token_len = 0;
		rec->message = NULL;
	}
  oc_message_unref(msg);
	return OC_EVENT_DONE;
}

void oc_replay_message_untrack(struct oc_message_s* msg)
{
	/*
    1. set tracked msg to NULL, set tracked token len to '0'
    3. remove delayed callback
   */

  oc_replay_free_msg_handler(msg);
	oc_remove_delayed_callback(msg, oc_replay_free_msg_handler);
}

void oc_replay_message_track(struct oc_message_s* msg, uint16_t token_len, const uint8_t* token)
{
	/*
	  1. add ref
		2. set token
		3. set delayed callback
   */

  struct oc_cached_message_record* rec = find_empty_record();

  if (!rec)
		return;

	// from here on a rec entry is available 

	// add reference
  oc_message_add_ref(msg);

	// pointer to (void) method defined a bit above, the required parameter will be passed to it later on when called 
	msg->soft_ref_cb = oc_replay_message_untrack; 

	// save token
	rec->token_len = token_len;
	memcpy(rec->token, token, token_len);
	rec->message = msg;

	oc_set_delayed_callback(msg, oc_replay_free_msg_handler, OC_REPLAY_RECORD_TIMEOUT);
}
