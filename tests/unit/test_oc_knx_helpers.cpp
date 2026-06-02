/*
 * Unit tests for api/oc_knx_helpers.c
 *
 * Covers:
 *   collect_and_rank_status   — accumulate worst CoAP status
 *   add_next_page_indicator   — frame ";rt=p.next" pagination link
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oc_ri.h"
#include "oc_rep.h"
#include "api/oc_knx_helpers.h"
#include "messaging/coap/constants.h"
}

/* ═══════════════════════════════════════════════════════════════════════════
 * collect_and_rank_status — pure function, no init needed
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(CollectAndRank, InitialOkStaysOk)
{
  oc_status_t status = OC_STATUS_OK;
  collect_and_rank_status(CONTENT_2_05, &status);
  EXPECT_EQ(status, OC_STATUS_OK);
}

TEST(CollectAndRank, HigherStatusOverrides)
{
  oc_status_t status = OC_STATUS_OK;
  collect_and_rank_status(BAD_REQUEST_4_00, &status);
  EXPECT_GT((int)status, (int)OC_STATUS_OK);
  EXPECT_EQ(status, OC_STATUS_BAD_REQUEST);
}

TEST(CollectAndRank, LowerStatusDoesNotOverride)
{
  oc_status_t status = OC_STATUS_BAD_REQUEST;
  collect_and_rank_status(CONTENT_2_05, &status);
  EXPECT_EQ(status, OC_STATUS_BAD_REQUEST);
}

TEST(CollectAndRank, MultipleCallsKeepWorst)
{
  oc_status_t status = OC_STATUS_OK;
  collect_and_rank_status(CONTENT_2_05, &status);
  collect_and_rank_status(CHANGED_2_04, &status);
  collect_and_rank_status(BAD_REQUEST_4_00, &status);
  collect_and_rank_status(CONTENT_2_05, &status);
  EXPECT_EQ(status, OC_STATUS_BAD_REQUEST);
}

TEST(CollectAndRank, NotFoundHigherThanBadRequest)
{
  oc_status_t status = OC_STATUS_OK;
  collect_and_rank_status(BAD_REQUEST_4_00, &status);
  oc_status_t after_bad = status;
  collect_and_rank_status(NOT_FOUND_4_04, &status);
  EXPECT_GE((int)status, (int)after_bad);
}

TEST(CollectAndRank, InternalServerError)
{
  oc_status_t status = OC_STATUS_OK;
  collect_and_rank_status(INTERNAL_SERVER_ERROR_5_00, &status);
  EXPECT_EQ(status, OC_STATUS_INTERNAL_SERVER_ERROR);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * add_next_page_indicator — needs oc_rep buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

class NextPageTest : public ::testing::Test {
protected:
  uint8_t buf[256];

  void SetUp() override {
    oc_rep_new(buf, sizeof(buf));
  }
};

TEST_F(NextPageTest, PageOne)
{
  char url[] = "/fp/r";
  int len = add_next_page_indicator(url, 1);
  EXPECT_GT(len, 0);

  /* The buffer should contain ,</fp/r?pn=1>;rt="p.next";ct=40 */
  std::string result(reinterpret_cast<char *>(buf), len);
  EXPECT_NE(result.find("/fp/r?pn=1"), std::string::npos);
  EXPECT_NE(result.find("rt=\"p.next\""), std::string::npos);
  EXPECT_NE(result.find("ct=40"), std::string::npos);
}

TEST_F(NextPageTest, PageZero)
{
  char url[] = "/fp/g";
  int len = add_next_page_indicator(url, 0);
  EXPECT_GT(len, 0);

  std::string result(reinterpret_cast<char *>(buf), len);
  EXPECT_NE(result.find("/fp/g?pn=0"), std::string::npos);
}

TEST_F(NextPageTest, LargePageNumber)
{
  char url[] = "/fp/r";
  int len = add_next_page_indicator(url, 9999);
  EXPECT_GT(len, 0);

  std::string result(reinterpret_cast<char *>(buf), len);
  EXPECT_NE(result.find("pn=9999"), std::string::npos);
}
