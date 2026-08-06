#include "holonight_domain/conversation.h"

#include <QDateTime>
#include <QString>

#include <chrono>
#include <gtest/gtest.h>
#include <set>
#include <thread>

namespace holonight_domain {
namespace {

TEST(ConversationId, GenerateProducesUniqueValues) {
  std::set<QString> seen;
  for (int i = 0; i < 100; ++i) {
    seen.insert(ConversationId::generate().toString());
  }
  EXPECT_EQ(seen.size(), 100U);
}

TEST(ConversationId, FromStringRoundTripsToString) {
  const ConversationId original = ConversationId::generate();
  const ConversationId reconstructed = ConversationId::fromString(original.toString());

  EXPECT_EQ(reconstructed, original);
  EXPECT_EQ(reconstructed.toString(), original.toString());
}

TEST(Conversation, ConstructionAndAccessors) {
  const ConversationId conversationId = ConversationId::generate();
  const QDateTime createdAt = QDateTime::currentDateTimeUtc();
  const Conversation conversation(conversationId, QString("Title"), createdAt);

  EXPECT_EQ(conversation.id(), conversationId);
  EXPECT_EQ(conversation.title(), QString("Title"));
  EXPECT_EQ(conversation.createdAt(), createdAt);
  EXPECT_EQ(conversation.updatedAt(), createdAt);
  EXPECT_TRUE(conversation.messages().empty());
}

TEST(Conversation, AppendMessagePreservesInsertionOrder) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());

  const Message first(MessageId::generate(), MessageRole::User, QString("first"));
  const Message second(MessageId::generate(), MessageRole::Assistant, QString("second"));
  const Message third(MessageId::generate(), MessageRole::User, QString("third"));

  conversation.appendMessage(first);
  conversation.appendMessage(second);
  conversation.appendMessage(third);

  ASSERT_EQ(conversation.messages().size(), 3U);
  EXPECT_EQ(conversation.messages()[0], first);
  EXPECT_EQ(conversation.messages()[1], second);
  EXPECT_EQ(conversation.messages()[2], third);
}

TEST(Conversation, AppendMessageUpdatesUpdatedAt) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  const QDateTime before = conversation.updatedAt();

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  conversation.appendMessage(Message(MessageId::generate(), MessageRole::User, QString("hi")));

  EXPECT_GT(conversation.updatedAt(), before);
}

TEST(Conversation, ReplaceLastMessageReplacesInPlace) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  const Message first(MessageId::generate(), MessageRole::User, QString("first"));
  const MessageId secondId = MessageId::generate();
  const Message second(secondId, MessageRole::Assistant, QString("second"));
  conversation.appendMessage(first);
  conversation.appendMessage(second);

  const Message replacement(secondId, MessageRole::Assistant, QString("replaced"), MessageStatus::Complete);
  const bool replaced = conversation.replaceLastMessage(replacement);

  ASSERT_TRUE(replaced);
  ASSERT_EQ(conversation.messages().size(), 2U);
  EXPECT_EQ(conversation.messages()[0], first);
  EXPECT_EQ(conversation.messages()[1], replacement);
}

TEST(Conversation, ReplaceLastMessageUpdatesUpdatedAt) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  conversation.appendMessage(Message(MessageId::generate(), MessageRole::User, QString("hi")));
  const QDateTime before = conversation.updatedAt();

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  conversation.replaceLastMessage(Message(MessageId::generate(), MessageRole::Assistant, QString("bye")));

  EXPECT_GT(conversation.updatedAt(), before);
}

TEST(Conversation, ReplaceLastMessageOnEmptyConversationIsNoOp) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  const QDateTime before = conversation.updatedAt();

  const bool replaced =
      conversation.replaceLastMessage(Message(MessageId::generate(), MessageRole::Assistant, QString("x")));

  EXPECT_FALSE(replaced);
  EXPECT_TRUE(conversation.messages().empty());
  EXPECT_EQ(conversation.updatedAt(), before);
}

TEST(Conversation, EqualityComparesAttributesAndMessages) {
  Conversation lhs(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  lhs.appendMessage(Message(MessageId::generate(), MessageRole::User, QString("hi")));

  const Conversation identicalCopy = lhs;
  EXPECT_EQ(lhs, identicalCopy);

  Conversation withExtraMessage = lhs;
  withExtraMessage.appendMessage(Message(MessageId::generate(), MessageRole::Assistant, QString("more")));
  EXPECT_NE(lhs, withExtraMessage);
}

TEST(Conversation, SetTitleReplacesTitleAndUpdatesUpdatedAt) {
  Conversation conversation(ConversationId::generate(), QString("Title"), QDateTime::currentDateTimeUtc());
  const QDateTime before = conversation.updatedAt();

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  conversation.setTitle(QString("New Title"));

  EXPECT_EQ(conversation.title(), QString("New Title"));
  EXPECT_GT(conversation.updatedAt(), before);
}

TEST(DeriveConversationTitle, ReturnsFirstLineUnchangedWhenShorterThanLimit) {
  EXPECT_EQ(deriveConversationTitle(QString("hello world")), QString("hello world"));
}

TEST(DeriveConversationTitle, TruncatesFirstLineAtFortyEightCharsWithEllipsis) {
  const QString longLine(60, QChar('a'));
  const QString title = deriveConversationTitle(longLine);

  EXPECT_EQ(title, QString(48, QChar('a')) + QString::fromUtf8("\xE2\x80\xA6"));
}

TEST(DeriveConversationTitle, StopsAtFirstNewlineEvenIfShorterThanLimit) {
  EXPECT_EQ(deriveConversationTitle(QString("first line\nsecond line")), QString("first line"));
}

TEST(DeriveConversationTitle, ReturnsUntitledForEmptyFirstLine) {
  EXPECT_EQ(deriveConversationTitle(QString("")), QString("Untitled"));
  EXPECT_EQ(deriveConversationTitle(QString("\nsecond line")), QString("Untitled"));
  EXPECT_EQ(deriveConversationTitle(QString("   \nsecond line")), QString("Untitled"));
}

TEST(Conversation, MessageOrderIsSignificantForEquality) {
  const Message first(MessageId::generate(), MessageRole::User, QString("first"));
  const Message second(MessageId::generate(), MessageRole::Assistant, QString("second"));

  const std::vector<Message> forward{first, second};
  const std::vector<Message> reversed{second, first};

  EXPECT_NE(forward, reversed);
}

}  // namespace
}  // namespace holonight_domain
