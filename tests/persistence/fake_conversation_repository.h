#pragma once

#include "holonight_persistence/conversation_record.h"
#include "holonight_persistence/conversation_repository.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

#include <algorithm>
#include <holonight_domain/holonight_domain.h>
#include <optional>
#include <utility>
#include <vector>

namespace holonight_persistence {

// Test double for ConversationRepository (REQ-C-006/NF-008). Every method fires its result signal
// synchronously and inline — no QThread, no event loop needed — so ChatViewModel tests stay
// deterministic and fast, mirroring FakeHttpClient's style.
class FakeConversationRepository : public ConversationRepository {
 public:
  explicit FakeConversationRepository(QObject* parent = nullptr) : ConversationRepository(parent) {}

  void initialize() override {
    if (!initialization_failure_reason_.isEmpty()) {
      emit unavailable(initialization_failure_reason_);
      return;
    }
    emit initialized();
  }

  void createConversation(QString title) override {
    const QString id = holonight_domain::ConversationId::generate().toString();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const ConversationSummary summary{
        .id = id,
        .title = title.isEmpty() ? kDefaultConversationTitle : title,
        .created_at = now,
        .updated_at = now,
        .last_model_id = std::nullopt,
        .title_source = holonight_domain::TitleSource::Fallback,
    };
    conversations_.insert(id, summary);
    messages_.insert(id, {});
    emit conversationCreated(summary);
  }

  void materializeConversation(ConversationSummary conversation,
                               std::vector<holonight_domain::Message> initialMessages) override {
    ++materialize_conversation_call_count_;
    conversations_.insert(conversation.id, conversation);
    messages_.insert(conversation.id, std::move(initialMessages));
    emit conversationCreated(conversation);
  }

  void listConversations() override {
    ++list_conversations_call_count_;
    QList<ConversationSummary> result = conversations_.values();
    std::sort(result.begin(), result.end(), [](const ConversationSummary& lhs, const ConversationSummary& rhs) {
      return lhs.updated_at > rhs.updated_at;
    });
    emit conversationListLoaded(result);
  }

  void loadConversation(QString conversationId) override {
    if (!conversations_.contains(conversationId)) {
      emit error(conversationId, QStringLiteral("conversation not found"));
      return;
    }
    emit conversationLoaded(LoadedConversation{
        .summary = conversations_.value(conversationId),
        .messages = messages_.value(conversationId),
    });
  }

  void renameConversation(QString conversationId, QString newTitle,
                          holonight_domain::TitleSource titleSource = holonight_domain::TitleSource::Manual) override {
    if (!next_rename_failure_.isEmpty()) {
      const QString reason = std::exchange(next_rename_failure_, QString());
      emit error(conversationId, reason);
      return;
    }
    if (!conversations_.contains(conversationId)) {
      emit error(conversationId, QStringLiteral("conversation not found"));
      return;
    }
    ConversationSummary summary = conversations_.value(conversationId);
    if (titleSource == holonight_domain::TitleSource::Generated &&
        summary.title_source != holonight_domain::TitleSource::Fallback) {
      return;
    }
    summary.title = newTitle;
    summary.title_source = titleSource;
    summary.updated_at = QDateTime::currentDateTimeUtc();
    conversations_.insert(conversationId, summary);
    emit conversationRenamed(summary);
  }

  void deleteConversation(QString conversationId) override {
    conversations_.remove(conversationId);
    messages_.remove(conversationId);
    emit conversationDeleted(conversationId);
  }

  void pinConversation(QString conversationId) override {
    if (!conversations_.contains(conversationId)) {
      return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    ConversationSummary summary = conversations_.value(conversationId);
    summary.pinned_at = now;
    conversations_.insert(conversationId, summary);
    emit conversationPinned(conversationId, now);
  }

  void unpinConversation(QString conversationId) override {
    if (!conversations_.contains(conversationId)) {
      return;
    }
    ConversationSummary summary = conversations_.value(conversationId);
    summary.pinned_at = std::nullopt;
    conversations_.insert(conversationId, summary);
    emit conversationUnpinned(conversationId);
  }

  void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) override {
    ++update_last_model_id_call_count_;
    if (!conversations_.contains(conversationId)) {
      return;
    }
    ConversationSummary summary = conversations_.value(conversationId);
    summary.last_model_id = modelId;
    summary.updated_at = QDateTime::currentDateTimeUtc();
    conversations_.insert(conversationId, summary);
    emit lastModelIdUpdated(conversationId, modelId, summary.updated_at);
  }

  void persistNewMessage(QString conversationId, holonight_domain::Message message) override {
    ++persist_new_message_call_count_;
    messages_[conversationId].push_back(std::move(message));
  }

  void persistMessageSettled(QString conversationId, holonight_domain::Message message) override {
    ++persist_message_settled_call_count_;
    auto& messages = messages_[conversationId];
    for (auto& existing : messages) {
      if (existing.id() == message.id()) {
        existing = std::move(message);
        return;
      }
    }
  }

  void persistUsage(QString conversationId, QString messageId, QString modelIdentifier,
                    holonight_domain::Usage usage) override {
    ++persist_usage_call_count_;
    const UsageRecord record = toUsageRecord(conversationId, messageId, modelIdentifier, usage);
    usage_by_conversation_[conversationId].insert(messageId, record);
    emit usagePersisted(conversationId, messageId, record);

    last_persist_usage_conversation_id_ = std::move(conversationId);
    last_persist_usage_message_id_ = std::move(messageId);
    last_persist_usage_model_identifier_ = std::move(modelIdentifier);
    last_persist_usage_ = usage;
  }

  void usageForConversation(QString conversationId) override {
    emit usageForConversationLoaded(conversationId, usage_by_conversation_.value(conversationId));
  }

  void simulateInitializationFailure(QString reason) { initialization_failure_reason_ = std::move(reason); }
  void failNextRename(QString reason) { next_rename_failure_ = std::move(reason); }

  [[nodiscard]] QList<ConversationSummary> allConversations() const { return conversations_.values(); }

  [[nodiscard]] std::vector<holonight_domain::Message> allMessages(const QString& conversationId) const {
    return messages_.value(conversationId);
  }

  [[nodiscard]] int persistNewMessageCallCount() const { return persist_new_message_call_count_; }
  [[nodiscard]] int persistMessageSettledCallCount() const { return persist_message_settled_call_count_; }
  [[nodiscard]] int updateLastModelIdCallCount() const { return update_last_model_id_call_count_; }
  [[nodiscard]] int materializeConversationCallCount() const { return materialize_conversation_call_count_; }
  [[nodiscard]] int listConversationsCallCount() const { return list_conversations_call_count_; }
  [[nodiscard]] int persistUsageCallCount() const { return persist_usage_call_count_; }
  [[nodiscard]] QString lastPersistUsageConversationId() const { return last_persist_usage_conversation_id_; }
  [[nodiscard]] QString lastPersistUsageMessageId() const { return last_persist_usage_message_id_; }
  [[nodiscard]] QString lastPersistUsageModelIdentifier() const { return last_persist_usage_model_identifier_; }
  [[nodiscard]] std::optional<holonight_domain::Usage> lastPersistUsage() const { return last_persist_usage_; }

 private:
  QHash<QString, ConversationSummary> conversations_;
  QHash<QString, std::vector<holonight_domain::Message>> messages_;
  QString initialization_failure_reason_;
  QString next_rename_failure_;
  int persist_new_message_call_count_ = 0;
  int persist_message_settled_call_count_ = 0;
  int update_last_model_id_call_count_ = 0;
  int materialize_conversation_call_count_ = 0;
  int list_conversations_call_count_ = 0;
  int persist_usage_call_count_ = 0;
  QString last_persist_usage_conversation_id_;
  QString last_persist_usage_message_id_;
  QString last_persist_usage_model_identifier_;
  std::optional<holonight_domain::Usage> last_persist_usage_;
  QHash<QString, QMap<QString, UsageRecord>> usage_by_conversation_;
};

}  // namespace holonight_persistence
