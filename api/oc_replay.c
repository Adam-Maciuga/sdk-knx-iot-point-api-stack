/*
// Copyright (c) 2023 Cascoda Ltd
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
#include "oc_replay.h"
#include "port/oc_clock.h"
#include "oc_config.h"
#include "messaging/coap/constants.h"
#include "oc_api.h"


#ifndef OC_MAX_REPLAY_RECORDS
#define OC_MAX_REPLAY_RECORDS (20) 
#endif

#ifndef OC_MAX_MESSAGE_RECORDS
#define OC_MAX_MESSAGE_RECORDS (2)
#endif

#ifndef OC_REPLAY_RECORD_TIMEOUT
#define OC_REPLAY_RECORD_TIMEOUT (5)
#endif

#ifndef OC_REPLAY_WINDOW_SIZE
#define OC_REPLAY_WINDOW_SIZE (32)
#endif

static struct oc_replay_record
{
	uint64_t rx_ssn;        // most recent received SSN of client
	oc_string_t rx_kid;     // byte string holding the KID of the client
	oc_string_t rx_kid_ctx; // byte string holding the KID context of the client, can be null
	oc_clock_time_t time;   // time of last received packet
	uint32_t window;        // bitfield indicating received SSNs through bit position, 32 = default by OSCORE RFC 
	bool in_use;            // whether this structure is in use & has valid data
} replay_records[OC_MAX_REPLAY_RECORDS] = { 0 };

static struct oc_cached_message_record
{
	uint16_t token_len;
	uint8_t token[COAP_TOKEN_LEN];
	struct oc_message_s* message;
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
	for (size_t i = 0; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		if (!replay_records[i].in_use)
			return replay_records + i;
	}
	// nothing free, free oldest record 

	// defines oldest record as first array element
	struct oc_replay_record* oldest_rec = replay_records;

	// finding of oldest record, to release it (on heap limitations) 
	for (size_t i = 1; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		if (replay_records[i].time < oldest_rec->time)
			oldest_rec = replay_records + i;
	}

	free_record(oldest_rec);
	return oldest_rec;
}

// find record with KID and CTX
static struct oc_replay_record* get_record(const oc_string_t rx_kid, const oc_string_t rx_kid_ctx)
{
	if (oc_byte_string_len(rx_kid) == 0)
	{
		return NULL; // rx kid not present -> a match is not applicable
	}

	for (size_t i = 0; i < OC_MAX_REPLAY_RECORDS; i++)
	{
		// c pointer arithmetics
		struct oc_replay_record* rec = replay_records + i;

		if (rec->in_use)
		{
			bool rx_kid_match = oc_byte_string_cmp(rx_kid, rec->rx_kid) == 0;
			bool null_contexts = oc_byte_string_len(rx_kid_ctx) == 0 && oc_byte_string_len(rec->rx_kid_ctx) == 0;
			bool contexts_match = oc_byte_string_cmp(rx_kid_ctx, rec->rx_kid_ctx) == 0;

			// kid match and (kid context both '0' = response or both match = request)
			if (rx_kid_match && (null_contexts || contexts_match))
				return rec;
		}
	}
	return NULL;  // nothing found
}

replay_state_t oc_replay_check_client(const uint64_t rx_ssn, const oc_string_t rx_kid, const oc_string_t rx_kid_ctx)
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

	The entire bitfield is left shifted whenever the recorded SSN increases,
	thus 'sliding' the window in a very efficient manner. Here's an example
	of the algorithm in operation, with a REDUCED bitfield for readability:

	max ssn = 8, bitfield = 0b0100'0001
	rx 6, bit 8 - 6 = 2, bit is not set -> delayed msg: accept msg & set bit 2
	:
	:
	max ssn = 8, bitfield = 0b0100'0101
	rx 2, bit 8 - 2 = 6, bit is set -> replayed msg: ssn 6 received again, throw msg
	rx 8, bit 8 - 8 = 0, bit is set -> replayed msg: ssn 8 received again, throw msg
	:
	:
	rx 9, bit 8 - 9 = -1, fresh msg: change ssn, left shift bitfield by 1, set bit 0
	max ssn = 9, bitfield = 0b1000'1011
	:
	:
	rx 99, bit 9 - 99 = bit -90, fresh msg: change ssn, left shift bitfield by 90, set bit 0
	max ssn = 99, bitfield = 0b0000'0001

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

  const int64_t ssn_diff = rec->rx_ssn - rx_ssn;  // SSN = 32 bit hence unproblematic

	PRINT("new ssn = %llu", rx_ssn);                // %llu = 64 bit ulong
	PRINT("old ssn = %llu", rec->rx_ssn);           // %llu = 64 bit ulong
	PRINT("kid     = %s", oc_string(rx_kid));       // %s = string
	PRINT("wnd old : %u", rec->window);             // %u = 32 bit ulong bit field 
	PRINT("ssn_diff = %lli", ssn_diff);             // 64 bit int

	// new SSN <= max value of received SSN -> either new SSN is within window or out of left bound  
	if (ssn_diff >= 0)
	{
		PRINT("ssn_diff = %lli >= 0", ssn_diff);

		// diff >= 32 -> out of left bound (max ssn = 32, rx = 0 -> SSN of 1..32 can be windowed) 
		if (ssn_diff >= OC_REPLAY_WINDOW_SIZE)
		{
			PRINT("out of window left bound");
			return ECHO; // not known if it was (ever) received before  
		}

		// diff < 32 -> within the window (max ssn = 32, rx = 1 -> SSN 1..31 can be windowed)
		// see if it has been received before, so this can be a replay
		if (rec->window & 1 << ssn_diff)
		{
			PRINT("within window, replay msg");
			return REPLAY; // known that it was received before 
		}

		// SSN not received before, tick that this SSN is now occupied
		// DO NOT remember SSN, it is not the highest one
		rec->window |= 1 << ssn_diff;

		PRINT("within window, new msg");
		return SYNCED;
	}

	// new SSN > old SSN -> fresh message, slide the window and accept the packet
	// note that shifting by an amount larger than the size of the type
	// is undefined behaviour, so we must zero the window manually here

	if (-ssn_diff >= OC_REPLAY_WINDOW_SIZE)
		rec->window = 0;            // 00000000'..'..'00000001' << 32 = 00000000'..'..'00000000'
	else
		rec->window <<= -ssn_diff;  // 00000000'..'..'00000001' << 31 = 10000000'..'..'00000000'

	// set bit 0, indicating ssn 'rec->rx_ssn' has been received
	// DO remember SSN, it is now the highest one
	rec->window |= 1;
	rec->rx_ssn = rx_ssn;

	PRINT("out of window right bound");
	return SYNCED;
}

void oc_replay_add_client(const uint64_t rx_ssn, const oc_string_t rx_kid, const oc_string_t rx_kid_ctx)
{
	struct oc_replay_record* rec = get_record(rx_kid, rx_kid_ctx);

	if (!rec)
	{ // no match
		rec = get_empty_record();
		oc_byte_string_copy(&rec->rx_kid, rx_kid);
		oc_byte_string_copy(&rec->rx_kid_ctx, rx_kid_ctx);
		rec->in_use = true;
	}

	// reinit record with fresh SSN + window, option to receive possible
	// older SSNs within the old window (if it was present) are gone  
	rec->rx_ssn = rx_ssn;
	rec->window = 1;
	rec->time = oc_clock_time();
}

void oc_replay_free_client(const oc_string_t rx_kid)
{
	for (size_t i = 0; i < OC_MAX_REPLAY_RECORDS; ++i)
	{
		struct oc_replay_record* rec = replay_records + i;
		if (oc_byte_string_cmp(rx_kid, rec->rx_kid) == 0)
		{
			free_record(rec);
		}
	}
}

struct oc_message_s* oc_replay_find_msg_by_token(const uint16_t token_len, const uint8_t* token)
{
	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; ++i)
	{
		if (message_records[i].message == NULL)
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
	if (msg == NULL)
		return NULL;

	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; ++i)
		if (message_records[i].message == msg)
			return message_records + i;
	return NULL;
}

static struct oc_cached_message_record* find_empty_msg_record(void)
{
	for (int i = 0; i < OC_MAX_MESSAGE_RECORDS; ++i)
		if (message_records[i].message == NULL)
			return message_records + i;
	return NULL;
}

static oc_event_callback_retval_t oc_replay_free_msg_handler(void* msg)
{
	struct oc_cached_message_record* rec = find_record_by_msg(msg);
	if (msg)
	{
		// OC_DBG("Freeing tracked message %p with token:", msg);
		// OC_LOGbytes(rec->token, rec->token_len);
		rec->token_len = 0;
		rec->message = NULL;
		oc_message_unref(msg);
	}
	return OC_EVENT_DONE;
}

void oc_replay_message_unref(struct oc_message_s* msg)
{
	oc_replay_free_msg_handler(msg);
	oc_remove_delayed_callback(msg, oc_replay_free_msg_handler);
}

void oc_replay_message_track(struct oc_message_s* msg, const uint16_t token_len, const uint8_t* token)
{
	struct oc_cached_message_record* rec = find_empty_msg_record();

  if (!rec)
		return;
	
	oc_message_add_ref(msg);
	msg->soft_ref_cb = oc_replay_message_unref;

	rec->token_len = token_len;
	memcpy(rec->token, token, token_len);
	rec->message = msg;

	oc_set_delayed_callback(msg, oc_replay_free_msg_handler, OC_REPLAY_RECORD_TIMEOUT);
}
