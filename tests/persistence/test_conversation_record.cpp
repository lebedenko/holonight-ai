#include "holonight_persistence/conversation_record.h"

#include <QDateTime>
#include <QString>

#include <gtest/gtest.h>

namespace holonight_persistence {
namespace {

TEST(ConversationSummary, DefaultConstructibleAndEqualityComparable) {
  const ConversationSummary lhs{};
  const ConversationSummary rhs{};
  EXPECT_EQ(lhs, rhs);

  ConversationSummary different{};
  different.id = QStringLiteral("id");
  EXPECT_NE(lhs, different);
}

TEST(LoadedConversation, DefaultConstructibleAndEqualityComparable) {
  const LoadedConversation lhs{};
  const LoadedConversation rhs{};
  EXPECT_EQ(lhs, rhs);
}

TEST(RegisterMetaTypes, CanBeCalledMultipleTimesIdempotently) {
  registerMetaTypes();
  registerMetaTypes();
  SUCCEED();
}

}  // namespace
}  // namespace holonight_persistence
