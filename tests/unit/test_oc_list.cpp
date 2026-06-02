/*
 * Unit tests for util/oc_list.c
 *
 * Pure linked-list operations — no platform or stack dependencies.
 */

#include <gtest/gtest.h>

extern "C" {
#include "util/oc_list.h"
}

/* ── Test node ─────────────────────────────────────────────────────────────── */

struct test_node
{
  struct test_node *next;
  int value;
};

/* ── Fixture ───────────────────────────────────────────────────────────────── */

class OcListTest : public ::testing::Test {
protected:
  void *list_storage = nullptr;
  oc_list_t list = reinterpret_cast<oc_list_t>(&list_storage);

  test_node nodes[6];

  void SetUp() override
  {
    oc_list_init(list);
    for (int i = 0; i < 6; ++i) {
      nodes[i].next = nullptr;
      nodes[i].value = i + 1; // values 1..6
    }
  }
};

/* ── oc_list_init ──────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Init_ListIsEmpty)
{
  EXPECT_EQ(oc_list_head(list), nullptr);
  EXPECT_EQ(oc_list_tail(list), nullptr);
  EXPECT_EQ(oc_list_length(list), 0);
}

/* ── oc_list_add ───────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Add_SingleItem)
{
  oc_list_add(list, &nodes[0]);
  EXPECT_EQ(oc_list_head(list), &nodes[0]);
  EXPECT_EQ(oc_list_tail(list), &nodes[0]);
  EXPECT_EQ(oc_list_length(list), 1);
}

TEST_F(OcListTest, Add_MultipleItems_OrderPreserved)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);

  EXPECT_EQ(oc_list_head(list), &nodes[0]);
  EXPECT_EQ(oc_list_tail(list), &nodes[2]);
  EXPECT_EQ(oc_list_length(list), 3);

  // Walk and verify order
  auto *n = static_cast<test_node *>(oc_list_head(list));
  EXPECT_EQ(n->value, 1);
  n = static_cast<test_node *>(oc_list_item_next(n));
  EXPECT_EQ(n->value, 2);
  n = static_cast<test_node *>(oc_list_item_next(n));
  EXPECT_EQ(n->value, 3);
  EXPECT_EQ(oc_list_item_next(n), nullptr);
}

/* ── oc_list_push ──────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Push_AddsToHead)
{
  oc_list_add(list, &nodes[0]);
  oc_list_push(list, &nodes[1]);

  EXPECT_EQ(oc_list_head(list), &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);
}

TEST_F(OcListTest, Push_RemovesDuplicate)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  // Push nodes[0] again — should remove it from its current position first
  oc_list_push(list, &nodes[0]);

  EXPECT_EQ(oc_list_head(list), &nodes[0]);
  EXPECT_EQ(oc_list_length(list), 2);
  auto *second = static_cast<test_node *>(oc_list_item_next(&nodes[0]));
  EXPECT_EQ(second, &nodes[1]);
}

/* ── oc_list_pop ───────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Pop_RemovesHead)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  void *popped = oc_list_pop(list);
  EXPECT_EQ(popped, &nodes[0]);
  EXPECT_EQ(oc_list_head(list), &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 1);
}

TEST_F(OcListTest, Pop_UntilEmpty)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  EXPECT_NE(oc_list_pop(list), nullptr);
  EXPECT_NE(oc_list_pop(list), nullptr);
  EXPECT_EQ(oc_list_pop(list), nullptr);
  EXPECT_EQ(oc_list_length(list), 0);
}

TEST_F(OcListTest, Pop_EmptyList_ReturnsNull)
{
  EXPECT_EQ(oc_list_pop(list), nullptr);
}

/* ── oc_list_chop ──────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Chop_RemovesTail)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);

  void *chopped = oc_list_chop(list);
  EXPECT_EQ(chopped, &nodes[2]);
  EXPECT_EQ(oc_list_tail(list), &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);
}

TEST_F(OcListTest, Chop_SingleItem)
{
  oc_list_add(list, &nodes[0]);

  void *chopped = oc_list_chop(list);
  EXPECT_EQ(chopped, &nodes[0]);
  EXPECT_EQ(oc_list_length(list), 0);
}

TEST_F(OcListTest, Chop_EmptyList_ReturnsNull)
{
  EXPECT_EQ(oc_list_chop(list), nullptr);
}

/* ── oc_list_head / oc_list_tail ───────────────────────────────────────────── */

TEST_F(OcListTest, Head_EmptyList_ReturnsNull)
{
  EXPECT_EQ(oc_list_head(list), nullptr);
}

TEST_F(OcListTest, Tail_EmptyList_ReturnsNull)
{
  EXPECT_EQ(oc_list_tail(list), nullptr);
}

TEST_F(OcListTest, HeadTail_SingleItem_AreSame)
{
  oc_list_add(list, &nodes[0]);
  EXPECT_EQ(oc_list_head(list), oc_list_tail(list));
}

/* ── oc_list_remove ─────────────────────────────────────────────────────── */

TEST_F(OcListTest, Remove_Head)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);

  oc_list_remove(list, &nodes[0]);
  EXPECT_EQ(oc_list_head(list), &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);
}

TEST_F(OcListTest, Remove_Middle)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);

  oc_list_remove(list, &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);
  auto *n = static_cast<test_node *>(oc_list_item_next(&nodes[0]));
  EXPECT_EQ(n, &nodes[2]);
}

TEST_F(OcListTest, Remove_Tail)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  oc_list_remove(list, &nodes[1]);
  EXPECT_EQ(oc_list_tail(list), &nodes[0]);
  EXPECT_EQ(oc_list_length(list), 1);
}

TEST_F(OcListTest, Remove_Nonexistent_NoOp)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  oc_list_remove(list, &nodes[3]); // not in list
  EXPECT_EQ(oc_list_length(list), 2);
}

/* ── oc_list_length ────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Length_EmptyList)
{
  EXPECT_EQ(oc_list_length(list), 0);
}

TEST_F(OcListTest, Length_AfterAddRemove)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);
  EXPECT_EQ(oc_list_length(list), 3);

  oc_list_remove(list, &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);

  oc_list_pop(list);
  EXPECT_EQ(oc_list_length(list), 1);
}

/* ── oc_list_insert ────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Insert_AfterItem)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[2]);

  oc_list_insert(list, &nodes[0], &nodes[1]);

  EXPECT_EQ(oc_list_length(list), 3);
  auto *n = static_cast<test_node *>(oc_list_item_next(&nodes[0]));
  EXPECT_EQ(n, &nodes[1]);
  n = static_cast<test_node *>(oc_list_item_next(n));
  EXPECT_EQ(n, &nodes[2]);
}

TEST_F(OcListTest, Insert_NullPrev_PushesToHead)
{
  oc_list_add(list, &nodes[0]);

  oc_list_insert(list, nullptr, &nodes[1]);

  EXPECT_EQ(oc_list_head(list), &nodes[1]);
  EXPECT_EQ(oc_list_length(list), 2);
}

TEST_F(OcListTest, Insert_AtTail)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  oc_list_insert(list, &nodes[1], &nodes[2]);

  EXPECT_EQ(oc_list_tail(list), &nodes[2]);
  EXPECT_EQ(oc_list_length(list), 3);
}

/* ── oc_list_copy ──────────────────────────────────────────────────────────── */

TEST_F(OcListTest, Copy_SharesElements)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);

  void *dest_storage = nullptr;
  oc_list_t dest = reinterpret_cast<oc_list_t>(&dest_storage);
  oc_list_init(dest);

  oc_list_copy(dest, list);

  EXPECT_EQ(oc_list_head(dest), oc_list_head(list));
  EXPECT_EQ(oc_list_length(dest), 2);
}

TEST_F(OcListTest, Copy_EmptyList)
{
  void *dest_storage = nullptr;
  oc_list_t dest = reinterpret_cast<oc_list_t>(&dest_storage);

  oc_list_copy(dest, list);

  EXPECT_EQ(oc_list_head(dest), nullptr);
  EXPECT_EQ(oc_list_length(dest), 0);
}

/* ── oc_list_item_next ─────────────────────────────────────────────────────── */

TEST_F(OcListTest, ItemNext_WalkList)
{
  oc_list_add(list, &nodes[0]);
  oc_list_add(list, &nodes[1]);
  oc_list_add(list, &nodes[2]);

  void *n = oc_list_head(list);
  EXPECT_EQ(n, &nodes[0]);
  n = oc_list_item_next(n);
  EXPECT_EQ(n, &nodes[1]);
  n = oc_list_item_next(n);
  EXPECT_EQ(n, &nodes[2]);
  n = oc_list_item_next(n);
  EXPECT_EQ(n, nullptr);
}

TEST_F(OcListTest, ItemNext_Null_ReturnsNull)
{
  EXPECT_EQ(oc_list_item_next(nullptr), nullptr);
}


