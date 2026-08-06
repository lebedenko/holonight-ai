#include "holonight_domain/message.h"

#include <QUuid>

namespace holonight_domain {

MessageId::MessageId(QString value) : value_(std::move(value)) {}

MessageId MessageId::generate() { return MessageId(QUuid::createUuid().toString(QUuid::WithoutBraces)); }

MessageId MessageId::fromString(QString value) { return MessageId(std::move(value)); }

QString MessageId::toString() const { return value_; }

Message::Message() : id_(MessageId::generate()), created_at_(QDateTime::currentDateTimeUtc()) {}

Message::Message(MessageId messageId, MessageRole role, QString text, MessageStatus status, const QDateTime& createdAt,
                 std::optional<ModelId> modelId, std::vector<ToolCallEntry> toolCalls)
    : id_(std::move(messageId)),
      role_(role),
      text_(std::move(text)),
      status_(status),
      created_at_(createdAt.isValid() ? createdAt : QDateTime::currentDateTimeUtc()),
      model_id_(role == MessageRole::Assistant ? std::move(modelId) : std::nullopt),
      tool_calls_(std::move(toolCalls)) {}

const MessageId& Message::id() const { return id_; }

MessageRole Message::role() const { return role_; }

const QString& Message::text() const { return text_; }

MessageStatus Message::status() const { return status_; }

QDateTime Message::createdAt() const { return created_at_; }

const std::optional<ModelId>& Message::modelId() const { return model_id_; }

const std::vector<ToolCallEntry>& Message::toolCalls() const { return tool_calls_; }

void Message::setText(QString text) { text_ = std::move(text); }

void Message::setModelId(std::optional<ModelId> modelId) {
  model_id_ = role_ == MessageRole::Assistant ? std::move(modelId) : std::nullopt;
}

void Message::setToolCalls(std::vector<ToolCallEntry> entries) { tool_calls_ = std::move(entries); }

std::expected<void, TransitionError> Message::transitionTo(MessageStatus next) {
  const bool allowed =
      (status_ == MessageStatus::Pending && next == MessageStatus::Streaming) ||
      (status_ == MessageStatus::Streaming &&
       (next == MessageStatus::Complete || next == MessageStatus::Error || next == MessageStatus::Cancelled));
  if (!allowed) {
    return std::unexpected(TransitionError{.from = status_, .attempted = next});
  }
  status_ = next;
  return {};
}

}  // namespace holonight_domain
