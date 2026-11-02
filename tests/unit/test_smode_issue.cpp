/*
  Unit tests: s-mode send path (oc_issue_s_mode_message in api/oc_knx_client.c), driven through the
  public oc_send_s_mode_multicast_message()

  The s-mode message is built and sent in ONE function: one message buffer (handed over to its
  transaction), CoAP NON POST to /k, the s-mode envelope as payload. No message is kept in a static
  between two calls.

  The network side is faked with the GNU linker --wrap facility (see tests/unit/CMakeLists.txt):
    --wrap=oc_main_initialized               -> true, so coap_send_transaction() runs
    --wrap=coap_send_message                 -> records the sent bytes and releases the message
    --wrap=oc_etimer_restart                 -> no-op (no transaction process in a unit test)
    --wrap=oc_allocate_message               -> counts message allocations
    --wrap=coap_new_transaction_with_message -> pass-through, or NULL to simulate "no transaction free"
*/

#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "oc_api.h"
#include "oc_buffer.h"
#include "api/oc_knx_client.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/transactions.h"
#include "port/oc_network_events_mutex.h"
#include "port/oc_random.h"
#include "util/oc_mmem.h"
}

/* -- linker --wrap stubs -------------------------------------------------- */

static int g_send_calls;
static int g_alloc_calls;
static bool g_fail_transaction;
static std::vector<uint8_t> g_sent_data;

extern "C" bool __wrap_oc_main_initialized(void)
{
  return true;
}

extern "C" void __wrap_coap_send_message(oc_message_t *msg)
{
  g_send_calls++;
  /* one spare byte like the real message buffer (OC_PDU_SIZE + 1): the parser terminates the payload in place */
  g_sent_data.assign(msg->data, msg->data + msg->length);
  g_sent_data.push_back(0);
  oc_message_unref(msg);
}

extern "C" void __wrap_oc_etimer_restart(struct oc_etimer *) {}

extern "C" oc_message_t *__real_oc_allocate_message(void);
extern "C" oc_message_t *__wrap_oc_allocate_message(void)
{
  g_alloc_calls++;
  return __real_oc_allocate_message();
}

extern "C" coap_transaction_t *__real_coap_new_transaction_with_message(uint16_t mid, const uint8_t *token, uint8_t token_len,
                                                                       oc_message_t *message);
extern "C" coap_transaction_t *__wrap_coap_new_transaction_with_message(uint16_t mid, const uint8_t *token, uint8_t token_len,
                                                                       oc_message_t *message)
{
  if (g_fail_transaction)
  {
    return nullptr;
  }
  return __real_coap_new_transaction_with_message(mid, token, token_len, message);
}

/* -- fixture -------------------------------------------------------------- */

class SModeIssue : public ::testing::Test {
protected:
  oc_group_table_t recipient_;

  void SetUp() override
  {
    oc_network_event_handler_mutex_init();
    oc_random_init();
    oc_mmem_init();
    g_send_calls = 0;
    g_alloc_calls = 0;
    g_fail_transaction = false;
    g_sent_data.clear();
    memset(&recipient_, 0, sizeof(recipient_));
    recipient_.grpid = 1;
    recipient_.iid = 1;
  }

  void TearDown() override
  {
    coap_free_all_transactions();
    oc_random_destroy();
    oc_network_event_handler_mutex_destroy();
  }
};

/* a written value as the GET handler delivers it: BF 01 F5 FF = { 1: true } */
static const uint8_t k_value[] = {0xBF, 0x01, 0xF5, 0xFF};

/*
  Multicast write: one message, sent as CoAP NON POST to /k with the s-mode envelope as payload
*/
TEST_F(SModeIssue, MulticastWriteSendsOneNonPostToK)
{
  oc_send_s_mode_multicast_message(5, 0x0101, 'w', k_value, (int)sizeof(k_value), &recipient_, nullptr);

  EXPECT_EQ(g_alloc_calls, 1);
  ASSERT_EQ(g_send_calls, 1);

  coap_packet_t pkt[1];
  ASSERT_EQ(coap_parse_udp_message(pkt, g_sent_data.data(), g_sent_data.size() - 1), COAP_NO_ERROR);
  EXPECT_EQ(pkt->type, COAP_TYPE_NON);
  EXPECT_EQ(pkt->code, COAP_POST);
  EXPECT_EQ(pkt->token_len, COAP_TOKEN_LEN);

  const char *path = nullptr;
  const size_t path_len = coap_get_header_uri_path(pkt, &path);
  EXPECT_EQ(std::string(path, path_len), "k");

  /* payload: the envelope { 4: sia, 5: { 7: ga, 6: "w", 1: true } } */
  ASSERT_GT(pkt->payload_len, 0U);
  EXPECT_EQ(pkt->payload[0], 0xBF);
  const std::vector<uint8_t> payload(pkt->payload, pkt->payload + pkt->payload_len);
  const uint8_t value[] = {0x01, 0xF5};
  EXPECT_NE(std::search(payload.begin(), payload.end(), value, value + sizeof(value)), payload.end());
}

/*
  Read request: no value -> the envelope without a value member, still one message
*/
TEST_F(SModeIssue, MulticastReadSendsOneMessage)
{
  oc_send_s_mode_multicast_message(5, 0x0101, 'r', nullptr, 0, &recipient_, nullptr);

  EXPECT_EQ(g_alloc_calls, 1);
  EXPECT_EQ(g_send_calls, 1);
}

/*
  No transaction free -> nothing sent, the single message is released (ASan/LSan: no leak)
*/
TEST_F(SModeIssue, NoTransactionReleasesTheMessage)
{
  g_fail_transaction = true;

  oc_send_s_mode_multicast_message(5, 0x0101, 'w', k_value, (int)sizeof(k_value), &recipient_, nullptr);

  EXPECT_EQ(g_alloc_calls, 1);
  EXPECT_EQ(g_send_calls, 0);
}
