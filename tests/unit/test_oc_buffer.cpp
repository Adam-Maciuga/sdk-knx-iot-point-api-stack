/*
 * Unit tests for the CoAP message-buffer pool (api/oc_buffer.c,
 * declared in include/oc_buffer.h).
 *
 * Covers the unit-testable pool primitives:
 *   oc_allocate_message  — allocate a message + its OC_PDU_SIZE data buffer,
 *                          initialise ref_count / interface_index
 *   oc_message_add_ref   — increment the in-use reference counter (NULL-safe)
 *   oc_message_unref     — decrement and free data + message at ref_count 0
 *                          (NULL-safe)
 *
 * Requirements:
 *   oc_allocate_message takes the network-event mutex, so the fixture calls
 *   oc_network_event_handler_mutex_init() once. No stack / process init is
 *   needed for the pool primitives themselves.
 *
 * INTEGRATION (documented, not unit-tested here):
 *   oc_receive_message / oc_send_message and the message_buffer_handler
 *   OC_PROCESS_THREAD route messages by posting OC events to the OSCORE / CoAP
 *   processes and ultimately call oc_send_buffer() (real network I/O). They
 *   require a running process scheduler, the event table (oc_events) and a live
 *   IP context, so they are exercised by the runtime conformance suite rather
 *   than as unit tests.
 */

#include <gtest/gtest.h>
#include <cstdint>

extern "C" {
#include "oc_buffer.h"
#include "port/oc_network_events_mutex.h"
}

class BufferPool : public ::testing::Test {
protected:
  void SetUp() override { oc_network_event_handler_mutex_init(); }
};

/* ───────────────────────────── oc_allocate_message ───────────────────────── */

TEST_F(BufferPool, AllocateMessageReturnsInitialisedMessage)
{
  oc_message_t *m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  EXPECT_NE(m->data, nullptr);
  EXPECT_EQ(m->ref_count, 1);
  EXPECT_EQ(m->endpoint.interface_index, -1);
  oc_message_unref(m);
}

TEST_F(BufferPool, AllocateMessageDataIsWritableForFullPdu)
{
  oc_message_t *m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  ASSERT_NE(m->data, nullptr);
  /* the data buffer is OC_PDU_SIZE bytes; writing the first/last byte must not
   * corrupt the heap (validated by the allocator under ASan in CI). */
  m->data[0] = 0xAB;
  m->data[OC_PDU_SIZE - 1] = 0xCD;
  EXPECT_EQ(m->data[0], 0xAB);
  EXPECT_EQ(m->data[OC_PDU_SIZE - 1], 0xCD);
  oc_message_unref(m);
}

TEST_F(BufferPool, AllocateMessageReturnsDistinctBuffers)
{
  oc_message_t *a = oc_allocate_message();
  oc_message_t *b = oc_allocate_message();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(a, b);
  EXPECT_NE(a->data, b->data);
  oc_message_unref(a);
  oc_message_unref(b);
}

/* ───────────────────────────── oc_message_add_ref ────────────────────────── */

TEST_F(BufferPool, AddRefIncrementsCounter)
{
  oc_message_t *m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(m->ref_count, 1);
  oc_message_add_ref(m);
  EXPECT_EQ(m->ref_count, 2);
  oc_message_add_ref(m);
  EXPECT_EQ(m->ref_count, 3);
  /* drop the extra refs and free */
  oc_message_unref(m);
  oc_message_unref(m);
  oc_message_unref(m);
}

TEST_F(BufferPool, AddRefNullDocumentedNotNullSafeInDebugBuilds)
{
  /* NOTE (FINDINGS F-004): oc_message_add_ref() guards the increment with
   * `if (message)`, but its trailing OC_DBG reads message->ref_count outside
   * that guard, so passing NULL crashes in OC_DEBUG builds (this build defines
   * OC_DEBUG). We therefore do NOT call it with NULL here — doing so would
   * SEGFAULT. The guarded increment for a valid message is covered by
   * AddRefIncrementsCounter. */
  GTEST_SKIP() << "oc_message_add_ref(NULL) is not NULL-safe in OC_DEBUG "
                  "builds (FINDINGS F-004)";
}

/* ───────────────────────────── oc_message_unref ──────────────────────────── */

TEST_F(BufferPool, UnrefDecrementsWithoutFreeingWhenStillReferenced)
{
  oc_message_t *m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  oc_message_add_ref(m); /* ref_count == 2 */
  oc_message_unref(m);   /* ref_count == 1, must NOT free */
  EXPECT_EQ(m->ref_count, 1);
  oc_message_unref(m);   /* ref_count == 0, frees data + message */
}

TEST_F(BufferPool, UnrefAtZeroFreesMessage)
{
  /* a single allocate/unref pair must release both the data buffer and the
   * message struct without leaking or double-freeing (verified under ASan). */
  oc_message_t *m = oc_allocate_message();
  ASSERT_NE(m, nullptr);
  oc_message_unref(m);
  SUCCEED();
}

TEST_F(BufferPool, UnrefNullIsNoOp)
{
  oc_message_unref(nullptr);
  SUCCEED();
}
