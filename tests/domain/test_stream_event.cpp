#include "holonight_domain/stream_event.h"

#include <QString>

#include <gtest/gtest.h>

namespace holonight_domain {
namespace {

TEST(StreamEvent, HoldsContentDelta) {
  const StreamEvent event = ContentDelta{.text = QString("chunk")};
  ASSERT_TRUE(std::holds_alternative<ContentDelta>(event));
  EXPECT_EQ(std::get<ContentDelta>(event).text, QString("chunk"));
}

TEST(StreamEvent, HoldsCompleted) {
  const StreamEvent event = Completed{};
  ASSERT_TRUE(std::holds_alternative<Completed>(event));
  EXPECT_FALSE(std::holds_alternative<ContentDelta>(event));
}

TEST(StreamEvent, HoldsError) {
  const StreamEvent event = Error{.message = QString("boom")};
  ASSERT_TRUE(std::holds_alternative<Error>(event));
  EXPECT_EQ(std::get<Error>(event).message, QString("boom"));
}

TEST(StreamEvent, HoldsCancelled) {
  const StreamEvent event = Cancelled{};
  ASSERT_TRUE(std::holds_alternative<Cancelled>(event));
}

TEST(StreamEvent, ContentDeltaEqualityWithIdenticalText) {
  const StreamEvent lhs = ContentDelta{.text = QString("chunk")};
  const StreamEvent rhs = ContentDelta{.text = QString("chunk")};
  EXPECT_EQ(lhs, rhs);
}

TEST(StreamEvent, CompletedInstancesAlwaysEqual) {
  const StreamEvent lhs = Completed{};
  const StreamEvent rhs = Completed{};
  EXPECT_EQ(lhs, rhs);
}

TEST(StreamEvent, ErrorInequalityWithDifferingMessage) {
  const StreamEvent lhs = Error{.message = QString("first")};
  const StreamEvent rhs = Error{.message = QString("second")};
  EXPECT_NE(lhs, rhs);
}

TEST(StreamEvent, DifferentVariantsAreNeverEqual) {
  const StreamEvent contentDelta = ContentDelta{.text = QString("chunk")};
  const StreamEvent completed = Completed{};
  EXPECT_NE(contentDelta, completed);
}

TEST(StreamEvent, CompletedDefaultsToNoUsage) {
  const Completed completed{};
  EXPECT_EQ(completed.usage, std::nullopt);
}

TEST(StreamEvent, CompletedWithUsageEqualsIdenticalInstance) {
  const Completed lhs{.usage = Usage{.input_tokens = 50}};
  const Completed rhs{.usage = Usage{.input_tokens = 50}};
  EXPECT_EQ(lhs, rhs);
  ASSERT_TRUE(lhs.usage.has_value());
  EXPECT_EQ(lhs.usage->input_tokens, 50);
}

}  // namespace
}  // namespace holonight_domain
