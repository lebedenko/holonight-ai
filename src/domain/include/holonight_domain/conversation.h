#pragma once

#include "holonight_domain/message.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace holonight_domain {

class ConversationId {
 public:
  static ConversationId generate();
  // Reconstructs a ConversationId from a previously-generated toString() value — used to restore
  // conversation identity when loading a persisted row (holonight_persistence).
  static ConversationId fromString(QString value);

  [[nodiscard]] QString toString() const;

  friend bool operator==(const ConversationId&, const ConversationId&) = default;

 private:
  explicit ConversationId(QString value);

  QString value_;
};

class Conversation {
 public:
  Conversation(ConversationId conversationId, QString title, const QDateTime& createdAt);

  [[nodiscard]] const ConversationId& id() const;
  [[nodiscard]] const QString& title() const;
  [[nodiscard]] QDateTime createdAt() const;
  [[nodiscard]] QDateTime updatedAt() const;
  [[nodiscard]] const std::vector<Message>& messages() const;

  void appendMessage(Message message);
  bool replaceLastMessage(Message replacement);
  bool replaceMessage(Message replacement);
  void setTitle(QString title);

  friend bool operator==(const Conversation&, const Conversation&) = default;

 private:
  ConversationId id_;
  QString title_;
  QDateTime created_at_;
  QDateTime updated_at_;
  std::vector<Message> messages_;
};

// Pure string transform (REQ-F-027): the first line of firstMessageText, or its first 48
// characters, whichever is shorter; "Untitled" if the first line is empty.
[[nodiscard]] QString deriveConversationTitle(const QString& firstMessageText);

}  // namespace holonight_domain
