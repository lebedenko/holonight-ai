#pragma once

#include "holonight_domain/model_id.h"
#include "holonight_domain/tool_call.h"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace holonight_domain {

enum class MessageRole : std::uint8_t { System, User, Assistant };
enum class MessageStatus : std::uint8_t { Pending, Streaming, Complete, Error, Cancelled };

struct TransitionError {
  MessageStatus from;
  MessageStatus attempted;

  friend bool operator==(const TransitionError&, const TransitionError&) = default;
};

class MessageId {
 public:
  static MessageId generate();
  // Reconstructs a MessageId from a previously-generated toString() value — used to restore
  // message identity when loading persisted rows (holonight_persistence).
  static MessageId fromString(QString value);

  [[nodiscard]] QString toString() const;

  friend bool operator==(const MessageId&, const MessageId&) = default;

 private:
  explicit MessageId(QString value);

  QString value_;
};

class Message {
 public:
  Message();
  Message(MessageId messageId, MessageRole role, QString text, MessageStatus status = MessageStatus::Pending,
          const QDateTime& createdAt = {}, std::optional<ModelId> modelId = std::nullopt,
          std::vector<ToolCallEntry> toolCalls = {});

  [[nodiscard]] const MessageId& id() const;
  [[nodiscard]] MessageRole role() const;
  [[nodiscard]] const QString& text() const;
  [[nodiscard]] MessageStatus status() const;
  [[nodiscard]] QDateTime createdAt() const;
  [[nodiscard]] const std::optional<ModelId>& modelId() const;
  [[nodiscard]] const std::vector<ToolCallEntry>& toolCalls() const;

  std::expected<void, TransitionError> transitionTo(MessageStatus next);
  void setText(QString text);
  void setModelId(std::optional<ModelId> modelId);
  void setToolCalls(std::vector<ToolCallEntry> entries);

  friend bool operator==(const Message&, const Message&) = default;

 private:
  MessageId id_;
  MessageRole role_ = MessageRole::User;
  QString text_;
  MessageStatus status_ = MessageStatus::Pending;
  QDateTime created_at_;
  std::optional<ModelId> model_id_;
  std::vector<ToolCallEntry> tool_calls_;
};

}  // namespace holonight_domain
