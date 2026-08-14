#include "holonight_domain/conversation.h"

#include <QUuid>

#include <algorithm>

namespace holonight_domain {

ConversationId::ConversationId(QString value) : value_(std::move(value)) {}

ConversationId ConversationId::generate() { return ConversationId(QUuid::createUuid().toString(QUuid::WithoutBraces)); }

ConversationId ConversationId::fromString(QString value) { return ConversationId(std::move(value)); }

QString ConversationId::toString() const { return value_; }

Conversation::Conversation(ConversationId conversationId, QString title, const QDateTime& createdAt)
    : id_(std::move(conversationId)), title_(std::move(title)), created_at_(createdAt), updated_at_(createdAt) {}

const ConversationId& Conversation::id() const { return id_; }

const QString& Conversation::title() const { return title_; }

QDateTime Conversation::createdAt() const { return created_at_; }

QDateTime Conversation::updatedAt() const { return updated_at_; }

const std::vector<Message>& Conversation::messages() const { return messages_; }

void Conversation::appendMessage(Message message) {
  messages_.push_back(std::move(message));
  updated_at_ = QDateTime::currentDateTimeUtc();
}

bool Conversation::replaceLastMessage(Message replacement) {
  if (messages_.empty()) {
    return false;
  }
  messages_.back() = std::move(replacement);
  updated_at_ = QDateTime::currentDateTimeUtc();
  return true;
}

bool Conversation::replaceMessage(Message replacement) {
  const auto iterator = std::ranges::find_if(
      messages_, [&replacement](const Message& message) { return message.id() == replacement.id(); });
  if (iterator == messages_.end()) {
    return false;
  }
  *iterator = std::move(replacement);
  updated_at_ = QDateTime::currentDateTimeUtc();
  return true;
}

void Conversation::setTitle(QString title) {
  title_ = std::move(title);
  updated_at_ = QDateTime::currentDateTimeUtc();
}

QString deriveConversationTitle(const QString& firstMessageText) {
  static constexpr qsizetype kMaxTitleLength = 48;

  const qsizetype newlineIndex = firstMessageText.indexOf(QLatin1Char('\n'));
  QString firstLine = (newlineIndex >= 0 ? firstMessageText.left(newlineIndex) : firstMessageText).trimmed();
  if (firstLine.isEmpty()) {
    return QStringLiteral("Untitled");
  }
  if (firstLine.length() > kMaxTitleLength) {
    return firstLine.left(kMaxTitleLength).trimmed() + QStringLiteral("…");
  }
  return firstLine;
}

}  // namespace holonight_domain
