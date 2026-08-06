#pragma once

#include "holonight_persistence/conversation_repository.h"
#include "holonight_persistence/detail/conversation_repository_worker.h"

#include <QThread>

namespace holonight_persistence {

// GUI-thread-resident façade (REQ-F-022/023/024). Owns a QThread and a private
// detail::ConversationRepositoryWorker moved onto it. Every public method here does nothing but
// package the request and hand it to the worker via a functor-based, Qt::QueuedConnection
// QMetaObject::invokeMethod() call — no SQL type is ever named in this class's own logic.
class SqliteConversationRepository : public ConversationRepository {
  Q_OBJECT

 public:
  explicit SqliteConversationRepository(QString databasePath, QObject* parent = nullptr);
  ~SqliteConversationRepository() override;

  SqliteConversationRepository(const SqliteConversationRepository&) = delete;
  SqliteConversationRepository& operator=(const SqliteConversationRepository&) = delete;
  SqliteConversationRepository(SqliteConversationRepository&&) = delete;
  SqliteConversationRepository& operator=(SqliteConversationRepository&&) = delete;

  void initialize() override;
  void createConversation(QString title = {}) override;
  void materializeConversation(ConversationSummary conversation,
                               std::vector<holonight_domain::Message> initialMessages) override;
  void listConversations() override;
  void loadConversation(QString conversationId) override;
  void renameConversation(QString conversationId, QString newTitle,
                          holonight_domain::TitleSource titleSource = holonight_domain::TitleSource::Manual) override;
  void deleteConversation(QString conversationId) override;
  void pinConversation(QString conversationId) override;
  void unpinConversation(QString conversationId) override;
  void updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) override;
  void persistNewMessage(QString conversationId, holonight_domain::Message message) override;
  void persistMessageSettled(QString conversationId, holonight_domain::Message message) override;
  void persistUsage(QString conversationId, QString messageId, QString modelIdentifier,
                    holonight_domain::Usage usage) override;
  void usageForConversation(QString conversationId) override;

 private:
  QThread worker_thread_;
  detail::ConversationRepositoryWorker* worker_;  // owned by worker_thread_ (moveToThread)
};

}  // namespace holonight_persistence
