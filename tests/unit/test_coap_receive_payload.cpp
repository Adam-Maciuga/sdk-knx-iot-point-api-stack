/*
  Unit tests: request payload size limits in coap_receive (messaging/coap/engine.c)

  A request WITHOUT Block1 carries its whole payload in one message:
  - up to ONE block (OC_BLOCK_SIZE)          : processed
  - above ONE block, below OC_MAX_APP_DATA_SIZE : 4.13 Request Entity Too Large with Size1 = block size, so the client
                                               switches to blockwise (RFC 7959 2.9.3), not a RST
  - from OC_MAX_APP_DATA_SIZE                : 4.13 already from the parser (coap.c, test_coap.cpp)

  coap_receive is called directly with a plain (unprotected) request; the size check comes before the resource lookup
  and the access control, so no resource, token or OSCORE context is needed.

  The network side is faked with the GNU linker --wrap facility (see tests/unit/CMakeLists.txt):
    --wrap=oc_main_initialized -> true, so coap_send_transaction() runs
    --wrap=coap_send_message   -> records the response bytes and releases the message
    --wrap=oc_etimer_restart   -> no-op (no transaction process in a unit test)
*/

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "oc_buffer.h"
#include "oc_buffer_settings.h"
#include "messaging/coap/coap.h"
#include "messaging/coap/engine.h"
#include "messaging/coap/transactions.h"
#include "port/oc_network_events_mutex.h"
#include "port/oc_random.h"
#include "util/oc_mmem.h"
}

/* -- linker --wrap stubs -------------------------------------------------- */

static int g_send_calls;
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

/* -- helpers -------------------------------------------------------------- */

/*
  a plain PUT /p/1 with 'payload_len' bytes of payload in ONE message (no Block1); the payload is appended behind the
  serialized header by hand, coap_set_payload would cap it at the block size
*/
static oc_message_t *make_put(coap_message_type_t type, uint16_t mid, size_t payload_len)
{
  oc_message_t *m = oc_allocate_message();
  if (!m)
  {
    return nullptr;
  }

  m->endpoint.flags = IPV6;
  m->endpoint.addr.ipv6.port = 5683;
  m->endpoint.addr.ipv6.address[0] = 0xFE;
  m->endpoint.addr.ipv6.address[1] = 0x80;
  m->endpoint.addr.ipv6.address[15] = 0x42;

  coap_packet_t pkt[1];
  const uint8_t token[] = {0x7A, 0x7B};
  coap_udp_init_message(pkt, type, COAP_PUT, mid);
  coap_set_token(pkt, token, sizeof(token));
  coap_set_header_uri_path(pkt, "/p/1", 4);
  coap_set_header_content_format(pkt, APPLICATION_CBOR);

  size_t len = coap_serialize_message(pkt, m->data);
  m->data[len++] = 0xFF; /* payload marker */
  memset(m->data + len, 0xA5, payload_len);
  m->length = len + payload_len;
  return m;
}

class CoapReceivePayload : public ::testing::Test {
protected:
  void SetUp() override
  {
    oc_network_event_handler_mutex_init();
    oc_random_init();
    oc_mmem_init();
    oc_coap_clear_request_history();
    g_send_calls = 0;
    g_sent_data.clear();
  }

  void TearDown() override
  {
    coap_free_all_transactions();
    oc_coap_clear_request_history();
    oc_random_destroy();
    oc_network_event_handler_mutex_destroy();
  }

  /* parses the recorded response */
  bool sent(coap_packet_t *pkt)
  {
    return g_send_calls == 1 && coap_parse_udp_message(pkt, g_sent_data.data(), g_sent_data.size() - 1) == COAP_NO_ERROR;
  }
};

/* -- tests ---------------------------------------------------------------- */

/*
  CON request with a payload above one block (but below the buffer): piggybacked 4.13 with Size1 = block size
*/
TEST_F(CoapReceivePayload, ConRequestAboveOneBlockGets413WithSize1)
{
  const size_t block = oc_get_block_size();
  oc_message_t *m = make_put(COAP_TYPE_CON, 0x1201, block + 88);
  ASSERT_NE(m, nullptr);

  coap_receive(m);
  oc_message_unref(m);

  coap_packet_t pkt[1];
  ASSERT_TRUE(sent(pkt));
  EXPECT_EQ(pkt->type, COAP_TYPE_ACK);
  EXPECT_EQ(pkt->code, REQUEST_ENTITY_TOO_LARGE_4_13);
  EXPECT_EQ(pkt->mid, 0x1201);
  ASSERT_EQ(pkt->token_len, 2);
  EXPECT_EQ(pkt->token[0], 0x7A);

  uint32_t size1 = 0;
  ASSERT_EQ(coap_get_header_size1(pkt, &size1), 1);
  EXPECT_EQ(size1, (uint32_t)block);
}

/*
  NON request with a payload above one block: NON 4.13 with Size1 = block size
*/
TEST_F(CoapReceivePayload, NonRequestAboveOneBlockGets413WithSize1)
{
  const size_t block = oc_get_block_size();
  oc_message_t *m = make_put(COAP_TYPE_NON, 0x1202, block + 1);
  ASSERT_NE(m, nullptr);

  coap_receive(m);
  oc_message_unref(m);

  coap_packet_t pkt[1];
  ASSERT_TRUE(sent(pkt));
  EXPECT_EQ(pkt->type, COAP_TYPE_NON);
  EXPECT_EQ(pkt->code, REQUEST_ENTITY_TOO_LARGE_4_13);

  uint32_t size1 = 0;
  ASSERT_EQ(coap_get_header_size1(pkt, &size1), 1);
  EXPECT_EQ(size1, (uint32_t)block);
}

/*
  exactly one block is no size problem: the request is processed further (no 4.13, no RST)
*/
TEST_F(CoapReceivePayload, RequestOfExactlyOneBlockIsNotRejectedForItsSize)
{
  const size_t block = oc_get_block_size();
  oc_message_t *m = make_put(COAP_TYPE_CON, 0x1203, block);
  ASSERT_NE(m, nullptr);

  coap_receive(m);
  oc_message_unref(m);

  coap_packet_t pkt[1];
  ASSERT_TRUE(sent(pkt));
  EXPECT_NE(pkt->type, COAP_TYPE_RST);
  EXPECT_NE(pkt->code, REQUEST_ENTITY_TOO_LARGE_4_13);
}
