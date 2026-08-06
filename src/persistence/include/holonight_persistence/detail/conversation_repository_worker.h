#pragma once

#include "holonight_persistence/conversation_record.h"

#include <QMap>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

#include <holonight_domain/holonight_domain.h>

namespace holonight_persistence::detail {

// Lives on the dedicated worker thread for its entire lifetime (REQ-F-022). Owns the single
// QSqlDatabase connection. Not part of the public API — constructed and owned exclusively by
// SqliteConversationRepository; its header is reachable specifically so
// tests/persistence/test_conversation_repository_worker_threading.cpp can white-box-verify thread
// affinity without SqliteConversationRepository needing to expose that as a public seam.
class ConversationRepositoryWorker : public QObject {
  Q_OBJECT

 public:
  // connectionName must be unique across the process (Qt requires this for QSqlDatabase);
  // SqliteConversationRepository always generates one internally, callers never supply it.
  ConversationRepositoryWorker(QString databasePath, QString connectionName, QObject* parent = nullptr);
  ~ConversationRepositoryWorker() override;

  ConversationRepositoryWorker(const ConversationRepositoryWorker&) = delete;
  ConversationRepositoryWorker& operator=(const ConversationRepositoryWorker&) = delete;
  ConversationRepositoryWorker(ConversationRepositoryWorker&&) = delete;
  ConversationRepositoryWorker& operator=(ConversationRepositoryWorker&&) = delete;

 public Q_SLOTS:
  void openAndMigrate();
  void createConversation(const QString& title);
  void materializeConversation(const holonight_persistence::ConversationSummary& conversation,
                               const std::vector<holonight_domain::Message>& initialMessages);
  void listConversations();
  void loadConversation(const QString& conversationId);
  void renameConversation(const QString& conversationId, const QString& newTitle,
                          holonight_domain::TitleSource titleSource);
  void deleteConversation(const QString& conversationId);
  void pinConversation(const QString& conversationId);
  void unpinConversation(const QString& conversationId);
  void updateLastModelId(const QString& conversationId, const holonight_domain::ModelId& modelId);
  void persistNewMessage(const QString& conversationId, const holonight_domain::Message& message);
  void persistMessageSettled(const QString& conversationId, const holonight_domain::Message& message);
  void persistUsage(const QString& conversationId, const QString& messageId, const QString& modelIdentifier,
                    const holonight_domain::Usage& usage);
  void usageForConversation(const QString& conversationId);

 Q_SIGNALS:
  void initialized();
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
  void error(QString conversationId, QString message);

 private:
  [[nodiscard]] QSqlDatabase connection();
  [[nodiscard]] QDateTime nextOperationTimestamp();
  void markUnavailable(const QString& reason);
  void reportOperationError(const QString& conversationId, const QSqlError& sqlError);
  [[nodiscard]] static bool isFatal(const QSqlError& sqlError);

  QString database_path_;
  QString connection_name_;
  QDateTime last_operation_timestamp_;
  bool available_ = false;
};

}  // namespace holonight_persistence::detail
