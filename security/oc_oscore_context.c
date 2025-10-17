/*
// Copyright (c) 2020 Intel Corporation
// Copyright (c) 2022-2023 Cascoda Ltd.
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

#if defined(OC_OSCORE)

#include "oc_oscore_context.h"
#include "messaging/coap/transactions.h"
#include "oc_api.h"
#include "oc_client_state.h"
#include "oc_oscore_crypto.h"
#include "api/oc_knx_sec.h"
#include "oc_rep.h"
#include "port/oc_log.h"

OC_LIST(contexts);
OC_MEMB(ctx_s, oc_oscore_context_t, 20);

void oc_oscore_free_lru_recipient_context(void)
{
  oc_oscore_context_t* lru_ctx;

  // get first context of list
  oc_oscore_context_t* ctx = lru_ctx = oc_list_head(contexts);

  while (ctx)
  {
    if (ctx->sender_id_len == 0 && ctx->last_used < lru_ctx->last_used)
      lru_ctx = ctx; // catch tmp copy and make it to the LRU item 

    ctx = ctx->next; // get next 
  }
  // release tmp copy
  oc_oscore_free_context(lru_ctx);
}

// checking against receiver in contexts
oc_oscore_context_t* oc_oscore_find_context_by_kid(uint8_t* kid, uint8_t kid_len)
{

  if (kid_len == 0)
    return NULL;

  // list start
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  PRINT("find context by kid : kid:(%d) : ", kid_len);
  oc_char_println_hex((char*) (kid), kid_len);

  while (ctx)
  {
    PRINT("-> recipient_id : ");
    oc_char_println_hex((char*) (ctx->recipient_id), ctx->recipient_id_len);

    if (kid_len == ctx->recipient_id_len && memcmp(kid, ctx->recipient_id, kid_len) == 0)
    {
      PRINT("find context by kid at auth/at index : %d", ctx->auth_at_index);
      ctx->last_used = oc_clock_time();
      return ctx;
    }
    ctx = ctx->next;
  }
  return ctx;
}

oc_oscore_context_t* oc_oscore_find_context_by_kid_and_kid_context(uint8_t* kid, uint8_t kid_len, uint8_t* kid_ctx, uint8_t kid_ctx_len)
{
  
  // get list start
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  if (kid_len == 0)
    return NULL;

  while (ctx)
  {
    // debugging  
    PRINT("---> scanning oscore context list (rcv) id:");
    oc_char_println_hex((char*) ctx->recipient_id, ctx->recipient_id_len);

    // received frame kid (Sender ID) and kid_context (ID Context) must both match in size and value to an oscore context 
    if (kid_len == ctx->recipient_id_len
        && memcmp(kid, ctx->recipient_id, kid_len) == 0 
        && kid_ctx_len == ctx->id_context_len 
        && memcmp(kid_ctx, ctx->id_context, kid_ctx_len) == 0)
    {

      PRINT("found oscore context, with auth/at index: %d",ctx->auth_at_index);

      // update time for a possible release of "last used" - if table is full
      ctx->last_used = oc_clock_time();
      return ctx;
    }
    ctx = ctx->next;
  }

  // here ctx is NULL
  return ctx;
}

oc_oscore_context_t* oc_oscore_find_context_by_token_mid(uint8_t* token,
                                    uint8_t token_len, uint16_t mid,
                                    uint8_t** request_piv,
                                    uint8_t* request_piv_len, bool tcp)
{
  char* oscore_id = NULL;
  size_t oscore_id_len = 0;

#ifdef OC_CLIENT

  // search for client cb by token
  oc_client_cb_t* cb = oc_ri_find_client_cb_by_token(token, token_len);

  if (cb)
  {
    if (request_piv && request_piv_len)
    {
      *request_piv = cb->piv;
      *request_piv_len = cb->piv_len;
    }
    oscore_id = cb->endpoint.oscore_id;
    oscore_id_len = cb->endpoint.oscore_id_len;
  }
  else
  {

#endif 

    // search transactions by token
    coap_transaction_t* t = coap_get_transaction_by_token(token, token_len);
    if (!t)
    {
      if (!tcp)
      {
        // on NOT TCP search by mid 
        t = coap_get_transaction_by_mid(mid);
      }
      if (!t)
      {
        
        return NULL;
      }
    }
    if (request_piv && request_piv_len)
    {
      *request_piv = t->message->endpoint.request_piv;
      *request_piv_len = t->message->endpoint.request_piv_len;
    }
    
    oscore_id = t->message->endpoint.oscore_id;
    oscore_id_len = t->message->endpoint.oscore_id_len;

#ifdef OC_CLIENT
  }
#endif

  oc_oscore_context_t* ctx = oc_list_head(contexts);

  if (oscore_id_len == 0)
  {
    OC_ERR("***could not find matching OSCORE context: oscore_id is NULL***");
    return NULL;
  }

  while (ctx)
  {
    if (memcmp(oscore_id, ctx->sender_id, oscore_id_len) == 0)
    {
      PRINT("oc_oscore_find_context_by_token_mid FOUND auth/at index: %d", ctx->auth_at_index);
      ctx->last_used = oc_clock_time();
      return ctx;
    }
    ctx = ctx->next;
  }
  // is NULL
  return ctx;
}

oc_oscore_context_t* oc_oscore_find_context_by_oscore_id(char* oscore_id, size_t oscore_id_len)
{
  int cmp_len = 16;

  if (oscore_id_len > 16)
  {
    OC_ERR("oscore_id longer than 16: %d", (int) oscore_id_len);
    return NULL;
  }

  if (oscore_id_len == 0)
  {
    OC_ERR("oscore_id_len == 0");
    return NULL;
  }
  if (oscore_id == NULL)
  {
    OC_ERR("oscore_id NULL");
    return NULL;
  }
  if (oscore_id_len < 16)
  {
    cmp_len = oscore_id_len;
  }

  PRINT("oc_oscore_find_context_by_oscore_id:");
  oc_char_println_hex(oscore_id, oscore_id_len);

  oc_oscore_context_t* ctx = oc_list_head(contexts);
  while (ctx != NULL)
  {
    if (memcmp(oscore_id, ctx->sender_id, cmp_len) == 0)
    {
      PRINT("oc_oscore_find_context_by_oscore_id FOUND auth/at index : %d",  ctx->auth_at_index);
      OC_DBG_OSCORE("    Common IV :");
      OC_LOGbytes_OSCORE(ctx->common_iv, OSCORE_COMMON_IV_LEN);
      ctx->last_used = oc_clock_time();
      return ctx;
    }
    ctx = ctx->next;
  }
  PRINT("NOT FOUND");
  return ctx;
}

// find access token for a given group address
oc_oscore_context_t* oc_oscore_find_context_by_group_address(uint32_t group_address)
{
  // get first context of list
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  while (ctx)
  {
    // find AT for context that MAY host the GA
    const oc_auth_at_t* my_entry = oc_get_auth_at_entry(ctx->auth_at_index);
    if (my_entry)
    {
      // debugging 
      oc_print_auth_at_entry(ctx->auth_at_index);

      for (int i = 0; i < my_entry->ga_len; i++)
      { // scan all GA's
        const uint32_t group_value = my_entry->ga[i];
        
        if (group_address == group_value)
        {
          PRINT("found access token for GA %u", group_address);

          // refresh time of last use
          ctx->last_used = oc_clock_time();
          return ctx;
        }
      }
    }
    ctx = ctx->next;
  }
  // here NULL
  return ctx;
}

void oc_oscore_free_all_contexts(void)
{
  // get first context of list
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  while (ctx)
  {
    // tmp copy of next (if released its gone)
    oc_oscore_context_t* next = ctx->next;
    oc_oscore_free_context(ctx);
    // restore next ptr
    ctx = next;
  }

  oc_list_init(contexts);
}

void oc_oscore_free_sender_contexts(void)
{
  // get first context of list
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  while (ctx)
  {
    // tmp copy of next (if released its gone)
    oc_oscore_context_t* next = ctx->next;

    // release if context is not used as a "recipient" context
    if (ctx->recipient_id_len == 0)
      oc_oscore_free_context(ctx);
    // restore next ptr
    ctx = next;
  }
}

void oc_oscore_free_contexts_at_id(int auth_at_index)
{
  // get first context of list
  oc_oscore_context_t* ctx = oc_list_head(contexts);

  while (ctx)
  {
    // get temp copy
    oc_oscore_context_t* next = ctx->next;  

    if (ctx->auth_at_index == auth_at_index)
      oc_oscore_free_context(ctx);

    // use tmp copy, original may be NULL if released beforehand
    ctx = next; 
  }
}

void oc_oscore_free_context(oc_oscore_context_t* ctx)
{
  if (ctx)
  {
    // removes entry fom linked list
    oc_list_remove(contexts, ctx);
    // use global variable for the removal
    oc_memb_free(&ctx_s, ctx);
  }
}

oc_oscore_context_t* oc_oscore_add_context(const char* sender_id, int sender_id_size,
                                           const char* recipient_id, int recipient_id_size,
                                           uint64_t ssn, 
                                           const char* mastersecret, int mastersecret_size, 
                                           const char* salt, int salt_size, 
                                           const char* id_context, int id_context_size, 
                                           int auth_at_index,
                                           bool read_ssn_from_storage)
{

  //get a free sender context
  oc_oscore_context_t* ctx = oc_memb_alloc(&ctx_s);

  if (!ctx)
  {
    OC_ERR("No memory for allocating sender context");
    return NULL;
  }

  if (!sender_id && !recipient_id && !mastersecret)
  {
    OC_ERR("No sender ID or recipient ID or Master secret");
    goto add_oscore_context_error;
  }

  if (mastersecret_size < OSCORE_KEY_LEN ||
      mastersecret_size > OSCORE_MASTER_SECRET_LEN)
  {
    OC_ERR("master secret size is must be in range 16 ... 32 : %d", mastersecret_size);
    goto add_oscore_context_error;
  }

  if (sender_id_size > OSCORE_SENDER_ID_LEN)
  {
    OC_ERR("sender id size > %d = %d", OSCORE_SENDER_ID_LEN, sender_id_size);
    goto add_oscore_context_error;
  }

  if (recipient_id_size > OSCORE_SENDER_ID_LEN)
  {
    OC_ERR("recipient id size > %d = %d", OSCORE_SENDER_ID_LEN, recipient_id_size);
    goto add_oscore_context_error;
  }

  if (id_context_size > OSCORE_ID_CONTEXT_LEN) 
  {
    OC_ERR("osc ctx size > %d = %d", OSCORE_ID_CONTEXT_LEN, id_context_size);
    goto add_oscore_context_error;
  }

  ctx->ssn = ssn;
  ctx->auth_at_index = auth_at_index;
  ctx->last_used = oc_clock_time();

  /*
     To prevent SSN reuse, bump the SNN to a higher value that could've been previously
     used, considering any possible failed writes to a nonvolatile storage.
     RFC - Appendix B 1.1
   */
  if (read_ssn_from_storage)
  {
    ctx->ssn += OSCORE_SSN_WRITE_FREQ_K + OSCORE_SSN_PAD_F;
  }
 

  if (sender_id && sender_id_size > 0)
  {
    // set sender id to value from cnf:osc:id 
    memcpy(ctx->sender_id, sender_id, sender_id_size);
    ctx->sender_id_len = (uint8_t)sender_id_size;
  }

  if (recipient_id && recipient_id_size > 0)
  {
    memcpy(ctx->recipient_id, recipient_id, recipient_id_size);
    ctx->recipient_id_len = (uint8_t)recipient_id_size;
  }

  if (id_context && id_context_size > 0)
  {
    memcpy(ctx->id_context, id_context, id_context_size);
    ctx->id_context_len = (uint8_t)id_context_size;
  }
  
  if (mastersecret)
  {
    memcpy((char*) &ctx->master_secret, mastersecret, mastersecret_size);
  }

  PRINT("AT Index      : (%2d)  = ", auth_at_index);
  PRINT("Sender ID     : (%2d)  = ", ctx->sender_id_len); OC_LOGbytes_OSCORE(ctx->sender_id, ctx->sender_id_len);
  PRINT("Recipient ID  : (%2d)  = ", ctx->recipient_id_len); OC_LOGbytes_OSCORE(ctx->recipient_id, ctx->recipient_id_len);
  PRINT("ID Context    : (%2d)  = ", ctx->id_context_len);  OC_LOGbytes_OSCORE(ctx->id_context, ctx->id_context_len);
  PRINT("Master Secret : (%2d)  = ", mastersecret_size);  oc_char_println_hex(mastersecret, mastersecret_size);
  PRINT("Salt          : (%2d)  = ", salt_size);  oc_char_println_hex(salt, salt_size);
  PRINT("SSN           : (%llu) = ", ctx->ssn);

  if (oc_oscore_context_derive_param(
    ctx->sender_id, ctx->sender_id_len, ctx->id_context, ctx->id_context_len,
    "Key",
    (uint8_t*) mastersecret, mastersecret_size,
    (uint8_t*) salt, salt_size,
    ctx->sender_key, OSCORE_KEY_LEN) < 0)
  {
    OC_ERR("### error deriving Sender Key ...");
    goto add_oscore_context_error;
  }
  

  if (oc_oscore_context_derive_param(
    ctx->recipient_id, ctx->recipient_id_len, 
    ctx->id_context, ctx->id_context_len,
    "Key",
    (uint8_t*) mastersecret, mastersecret_size,
    (uint8_t*) salt, salt_size,
    ctx->recipient_key, OSCORE_KEY_LEN) < 0)
  {
    OC_ERR("### error deriving Recipient Key ...");
    goto add_oscore_context_error;
  }

  if (oc_oscore_context_derive_param(
    NULL, 0,
    ctx->id_context, ctx->id_context_len,
    "IV",
    (uint8_t*) mastersecret, mastersecret_size,
    (uint8_t*) salt, salt_size,
    ctx->common_iv, OSCORE_COMMON_IV_LEN) < 0)
  {
    OC_ERR("*** error deriving Common IV ###");
    goto add_oscore_context_error;
  }

  OC_DBG_OSCORE(PRINT16BYTEHEX("### derived Sender Key    : ", ctx->sender_key));
  OC_DBG_OSCORE(PRINT16BYTEHEX("### derived Recipient Key : ", ctx->recipient_key));
  OC_DBG_OSCORE(PRINT13BYTEHEX("### derived Common IV     : ", ctx->common_iv));

  oc_list_add(contexts, ctx);

  return ctx;

add_oscore_context_error:
  OC_DBG_OSCORE("Encountered error while adding new context!");
  oc_memb_free(&ctx_s, ctx);
  return NULL;
}

int oc_oscore_context_derive_param(const uint8_t* id, uint8_t id_len,
                               uint8_t* id_ctx, uint8_t id_ctx_len,
                               const char* type, uint8_t* secret,
                               uint8_t secret_len, uint8_t* salt,
                               uint8_t salt_len, uint8_t* param,
                               uint8_t param_len)
{
  uint8_t info[OSCORE_INFO_MAX_LEN];
  CborEncoder e, a;
  CborError err = CborNoError;

  /* From RFC 8613: Section 3.2.1:
      info = [
        id : bstr (byte string, cbor major type 2),
        id_context : bstr / nil,
        alg_aead : int / tstr (text string, cbor major type 3),
        type : tstr,
        L : uint,
      ]
  */
  cbor_encoder_init(&e, info, OSCORE_INFO_MAX_LEN, 0);
  /* Array of 5 elements */
  err |= cbor_encoder_create_array(&e, &a, 5);
  /* Sender ID, Recipient ID or empty string for Common IV */
  err |= cbor_encode_byte_string(&a, id, id_len);
  /* id_context or null if not provided */
  if (id_ctx_len > 0)
  {
    err |= cbor_encode_byte_string(&a, id_ctx, id_ctx_len);
  }
  else
  {
    err |= cbor_encode_null(&a);
  }
  /* alg_aead for AES-CCM-16-64-128 = 10 from RFC 8152 */
  err |= cbor_encode_int(&a, 10);
  /* type: "Key" or "IV" based on deriving a key of the Common IV */
  err |= cbor_encode_text_string(&a, type, strlen(type));
  /* Size of the key/nonce for the AEAD Algorithm used, in bytes */
  err |= cbor_encode_uint(&a, param_len);
  err |= cbor_encoder_close_container(&e, &a);

  if (err != CborNoError)
  {
    return -1;
  }

  return HKDF_SHA256(salt, salt_len, secret, secret_len, info,
                     cbor_encoder_get_buffer_size(&e, info), param, param_len);
}

#else  
typedef int dummy_declaration;
#endif 
