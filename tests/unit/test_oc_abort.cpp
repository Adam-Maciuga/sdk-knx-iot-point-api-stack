/*
 * Unit tests for port/linux/abort.c — the process termination port.
 *
 * Covers both public functions plus the inline wrappers in port/oc_assert.h
 * via GoogleTest death tests (the functions terminate the process):
 *   abort_impl  — calls abort()  -> SIGABRT
 *   exit_impl   — calls exit(s)  -> process exit with status s
 *   oc_abort    — inline wrapper around abort_impl
 *   oc_exit     — inline wrapper around exit_impl
 *   oc_assert   — aborts when the condition is false, no-op when true
 *
 * Note: this TU deliberately does NOT define its own abort_impl/exit_impl
 * stubs, so the linker pulls the real port/linux/abort.c objects from the
 * static library and we exercise the production implementations.
 */

#include <gtest/gtest.h>

extern "C" {
#include "port/oc_assert.h"
}

TEST(OcAbortDeathTest, AbortImplRaisesSigabrt)
{
  EXPECT_DEATH(abort_impl(), "");
}

TEST(OcAbortDeathTest, OcAbortWrapperTerminates)
{
  EXPECT_DEATH(oc_abort("boom"), "");
}

TEST(OcAbortDeathTest, ExitImplExitsWithGivenStatus)
{
  EXPECT_EXIT(exit_impl(3), ::testing::ExitedWithCode(3), "");
}

TEST(OcAbortDeathTest, OcExitWrapperExitsWithStatus)
{
  EXPECT_EXIT(oc_exit(7), ::testing::ExitedWithCode(7), "");
}

TEST(OcAbortDeathTest, AssertFalseAborts)
{
  EXPECT_DEATH(oc_assert(1 == 2), "");
}

TEST(OcAssert, AssertTrueIsNoOp)
{
  /* A satisfied assertion must not terminate the process. */
  oc_assert(1 == 1);
  SUCCEED();
}
