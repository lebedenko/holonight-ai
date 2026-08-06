#include "holonight_domain/tool_activity.h"

#include <QDateTime>
#include <QTimeZone>

#include <gtest/gtest.h>

namespace holonight_domain {
namespace {

TEST(ToolInvocation, DefaultStatusIsRequested) {
  const ToolInvocation invocation;
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Requested);
}

TEST(ToolInvocation, IdempotentTransitionToSameTerminalState) {
  ToolInvocation invocation;
  ASSERT_TRUE(invocation.transitionTo(ToolInvocationStatus::Failed).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Failed);

  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Failed).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Failed);
}

TEST(ToolInvocation, RequestedTransitionsToAwaitingApprovalAndRunning) {
  ToolInvocation invocation;

  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::AwaitingApproval).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::AwaitingApproval);

  invocation.status = ToolInvocationStatus::Requested;
  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Running);
}

TEST(ToolInvocation, RunningTransitionsToTerminalStates) {
  ToolInvocation invocation;
  invocation.status = ToolInvocationStatus::Running;

  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Completed).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Completed);

  invocation.status = ToolInvocationStatus::Running;
  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Failed).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Failed);

  invocation.status = ToolInvocationStatus::Running;
  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Cancelled).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Cancelled);
}

TEST(ToolInvocation, AwaitingApprovalTransitionsToRunningDeniedOrFailure) {
  ToolInvocation invocation;
  invocation.status = ToolInvocationStatus::AwaitingApproval;

  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Running);

  invocation.status = ToolInvocationStatus::AwaitingApproval;
  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Denied).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Denied);

  invocation.status = ToolInvocationStatus::AwaitingApproval;
  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Failed).has_value());
  EXPECT_EQ(invocation.status, ToolInvocationStatus::Failed);
}

TEST(ToolInvocation, TerminalStatesRejectBackwardTransitions) {
  ToolInvocation invocation;
  invocation.status = ToolInvocationStatus::Completed;
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  EXPECT_EQ(invocation.transitionTo(ToolInvocationStatus::Running).error().from, ToolInvocationStatus::Completed);

  invocation.status = ToolInvocationStatus::Denied;
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  EXPECT_EQ(invocation.transitionTo(ToolInvocationStatus::Running).error().from, ToolInvocationStatus::Denied);

  invocation.status = ToolInvocationStatus::Cancelled;
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Completed).has_value());
}

TEST(ToolInvocation, NonTerminalInvalidTransitionsAreRejected) {
  ToolInvocation invocation;
  invocation.status = ToolInvocationStatus::Requested;
  ASSERT_TRUE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Requested).has_value());

  invocation.status = ToolInvocationStatus::Requested;
  ASSERT_TRUE(invocation.transitionTo(ToolInvocationStatus::AwaitingApproval).has_value());
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Requested).has_value());

  invocation.status = ToolInvocationStatus::AwaitingApproval;
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::Completed).has_value());

  invocation.status = ToolInvocationStatus::Running;
  EXPECT_FALSE(invocation.transitionTo(ToolInvocationStatus::AwaitingApproval).has_value());
}

TEST(ToolInvocation, EqualityMatchesAllFields) {
  ToolInvocation lhs{.id = QStringLiteral("1"),
                     .provider_call_id = QStringLiteral("toolu_1"),
                     .tool_id = QStringLiteral("filesystem.list"),
                     .arguments = QJsonValue(1)};
  ToolInvocation rhs{.id = QStringLiteral("1"),
                     .provider_call_id = QStringLiteral("toolu_1"),
                     .tool_id = QStringLiteral("filesystem.list"),
                     .arguments = QJsonValue(1)};
  EXPECT_EQ(lhs, rhs);

  rhs.status = ToolInvocationStatus::Completed;
  EXPECT_NE(lhs, rhs);
}

TEST(ToolInvocation, TimestampsArePreservedAcrossTransition) {
  ToolInvocation invocation;
  const QDateTime requested(QDate(2026, 8, 5), QTime(12, 0), QTimeZone::UTC);
  invocation.requested_at = requested;

  EXPECT_TRUE(invocation.transitionTo(ToolInvocationStatus::Running).has_value());
  invocation.started_at = QDateTime(QDate(2026, 8, 5), QTime(12, 1), QTimeZone::UTC);
  EXPECT_EQ(invocation.requested_at, requested);
}

}  // namespace
}  // namespace holonight_domain
