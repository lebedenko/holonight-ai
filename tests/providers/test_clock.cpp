#include "fake_clock.h"
#include "holonight_providers/clock.h"

#include <gtest/gtest.h>

namespace holonight_providers {
namespace {

TEST(Clock, SteadyClockReturnsPositiveMilliseconds) {
  const SteadyClock clock;
  EXPECT_GT(clock.now().count(), 0);
}

TEST(Clock, FakeClockReturnsQueuedValuesInOrder) {
  FakeClock clock;
  clock.push(1000).push(2500);
  EXPECT_EQ(clock.now(), std::chrono::milliseconds(1000));
  EXPECT_EQ(clock.now(), std::chrono::milliseconds(2500));
}

}  // namespace
}  // namespace holonight_providers
