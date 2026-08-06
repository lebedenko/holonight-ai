#pragma once

#include "holonight_persistence/conversation_record.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence {

// Abstract interface (REQ-C-006). QObject-derived — results cross a real thread boundary
// (REQ-F-022), so this uses Qt's signal/slot system rather than std::function callbacks.
class ConversationRepository : public QObject {
  Q_OBJECT

 public:
  explicit ConversationRepository(QObject* parent = nullptr);
  ~ConversationRepository() override = default;

  ConversationRepository(const ConversationRepository&) = delete;
  ConversationRepository& operator=(const ConversationRepository&) = delete;
  ConversationRepository(ConversationRepository&&) = delete;
  ConversationRepository& operator=(ConversationRepository&&) = delete;

  // Opens/creates the database and applies pending migrations (REQ-F-001..004). Call exactly once
  // before any method below. Non-blocking; emits initialized() or unavailable(reason).
  virtual void initialize() = 0;

  // Every method below returns void immediately (REQ-F-023/REQ-NF-001); results/errors arrive later
  // via the signals below.
  virtual void createConversation(QString title = {}) = 0;
  virtual void materializeConversation(ConversationSummary conversation,
                                       std::vector<holonight_domain::Message> initialMessages) = 0;
  virtual void listConversations() = 0;
  virtual void loadConversation(QString conversationId) = 0;
  virtual void renameConversation(
      QString conversationId, QString newTitle,
      holonight_domain::TitleSource titleSource = holonight_domain::TitleSource::Manual) = 0;
  virtual void deleteConversation(QString conversationId) = 0;
  virtual void pinConversation(QString conversationId) = 0;
  virtual void unpinConversation(QString conversationId) = 0;
  virtual void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) = 0;
  virtual void persistNewMessage(QString conversationId, holonight_domain::Message message) = 0;
  virtual void persistMessageSettled(QString conversationId, holonight_domain::Message message) = 0;
  // Fire-and-forget, one row per call (REQ-F-009/010). Success surfaces via usagePersisted()
  // below (REQ-F-001) so footer-stats UI can render live; a non-fatal write failure surfaces via
  // the existing error() signal like every other repository method.
  virtual void persistUsage(QString conversationId, QString messageId, QString modelIdentifier,
                            holonight_domain::Usage usage) = 0;
  // Batch-loads every persisted usage row for a conversation (REQ-F-001/REQ-NF-001), keyed by
  // message id -- used once per conversation load, not per message, to populate footer stats for
  // historical messages. Result arrives via usageForConversationLoaded() below.
  virtual void usageForConversation(QString conversationId) = 0;

 Q_SIGNALS:
  void initialized();
  // Fires on startup failure (REQ-F-025) AND on a fatal mid-session failure (REQ-NF-005).
  void unavailable(QString reason);

  void conversationCreated(holonight_persistence::ConversationSummary conversation);
  void conversationListLoaded(QList<holonight_persistence::ConversationSummary> conversations);
  void conversationLoaded(holonight_persistence::LoadedConversation conversation);
  void conversationRenamed(holonight_persistence::ConversationSummary conversation);
  void conversationDeleted(QString conversationId);
  void conversationPinned(QString conversationId, QDateTime pinnedAt);
  void conversationUnpinned(QString conversationId);
  void lastModelIdUpdated(QString conversationId, holonight_domain::ModelId modelId, QDateTime updatedAt);
  void usagePersisted(QString conversationId, QString messageId, holonight_persistence::UsageRecord record);
  void usageForConversationLoaded(QString conversationId,
                                  QMap<QString, holonight_persistence::UsageRecord> usageByMessageId);

  // Non-fatal, single-operation failure (REQ-NF-004) — the repository remains usable afterward.
  void error(QString conversationId, QString message);
};

}  // namespace holonight_persistence
