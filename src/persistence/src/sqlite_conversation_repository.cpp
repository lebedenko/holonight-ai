#include "holonight_persistence/sqlite_conversation_repository.h"

#include <QUuid>

#include <utility>

namespace holonight_persistence {

using detail::ConversationRepositoryWorker;

SqliteConversationRepository::SqliteConversationRepository(QString databasePath, QObject* parent)
    : ConversationRepository(parent),
      worker_(new ConversationRepositoryWorker(std::move(databasePath),
                                               QUuid::createUuid().toString(QUuid::WithoutBraces))) {
  registerMetaTypes();
  worker_->moveToThread(&worker_thread_);

  // Documented Qt worker-object cleanup idiom: deleteLater() runs on the worker thread's own
  // finishing sequence, which is why the destructor below must call quit()+wait() rather than
  // `delete worker_` — deleting directly from the GUI thread would destroy a QObject whose
  // affinity is the worker thread.
  connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);

  connect(worker_, &ConversationRepositoryWorker::initialized, this, &ConversationRepository::initialized);
  connect(worker_, &ConversationRepositoryWorker::unavailable, this, &ConversationRepository::unavailable);
  connect(worker_, &ConversationRepositoryWorker::conversationCreated, this,
          &ConversationRepository::conversationCreated);
  connect(worker_, &ConversationRepositoryWorker::conversationListLoaded, this,
          &ConversationRepository::conversationListLoaded);
  connect(worker_, &ConversationRepositoryWorker::conversationLoaded, this,
          &ConversationRepository::conversationLoaded);
  connect(worker_, &ConversationRepositoryWorker::conversationRenamed, this,
          &ConversationRepository::conversationRenamed);
  connect(worker_, &ConversationRepositoryWorker::conversationDeleted, this,
          &ConversationRepository::conversationDeleted);
  connect(worker_, &ConversationRepositoryWorker::conversationPinned, this,
          &ConversationRepository::conversationPinned);
  connect(worker_, &ConversationRepositoryWorker::conversationUnpinned, this,
          &ConversationRepository::conversationUnpinned);
  connect(worker_, &ConversationRepositoryWorker::lastModelIdUpdated, this,
          &ConversationRepository::lastModelIdUpdated);
  connect(worker_, &ConversationRepositoryWorker::usagePersisted, this, &ConversationRepository::usagePersisted);
  connect(worker_, &ConversationRepositoryWorker::usageForConversationLoaded, this,
          &ConversationRepository::usageForConversationLoaded);
  connect(worker_, &ConversationRepositoryWorker::error, this, &ConversationRepository::error);

  worker_thread_.start();
}

SqliteConversationRepository::~SqliteConversationRepository() {
  // Drain every operation queued before destruction before asking the worker event loop to exit.
  // Without this barrier, closing the application immediately after a send can discard writes
  // that the GUI thread has already handed to the repository.
  if (worker_thread_.isRunning()) {
    QMetaObject::invokeMethod(worker_, [] {}, Qt::BlockingQueuedConnection);
  }
  worker_thread_.quit();
  worker_thread_.wait();
}

void SqliteConversationRepository::initialize() {
  QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->openAndMigrate(); }, Qt::QueuedConnection);
}

void SqliteConversationRepository::createConversation(QString title) {
  QMetaObject::invokeMethod(
      worker_, [worker = worker_, title = std::move(title)] { worker->createConversation(title); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::materializeConversation(ConversationSummary conversation,
                                                           std::vector<holonight_domain::Message> initialMessages) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversation = std::move(conversation), initialMessages = std::move(initialMessages)] {
        worker->materializeConversation(conversation, initialMessages);
      },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::listConversations() {
  QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->listConversations(); }, Qt::QueuedConnection);
}

void SqliteConversationRepository::loadConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId)] { worker->loadConversation(conversationId); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::renameConversation(QString conversationId, QString newTitle,
                                                      holonight_domain::TitleSource titleSource) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId), newTitle = std::move(newTitle), titleSource] {
        worker->renameConversation(conversationId, newTitle, titleSource);
      },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::deleteConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId)] { worker->deleteConversation(conversationId); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::pinConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId)] { worker->pinConversation(conversationId); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::unpinConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId)] { worker->unpinConversation(conversationId); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::updateLastModelId(QString conversationId, holonight_domain::ModelId modelId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId), modelId] {
        worker->updateLastModelId(conversationId, modelId);
      },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::persistNewMessage(QString conversationId, holonight_domain::Message message) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId), message = std::move(message)] {
        worker->persistNewMessage(conversationId, message);
      },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::persistMessageSettled(QString conversationId, holonight_domain::Message message) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId), message = std::move(message)] {
        worker->persistMessageSettled(conversationId, message);
      },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::persistUsage(QString conversationId, QString messageId, QString modelIdentifier,
                                                holonight_domain::Usage usage) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId), messageId = std::move(messageId),
       modelIdentifier = std::move(modelIdentifier),
       usage] { worker->persistUsage(conversationId, messageId, modelIdentifier, usage); },
      Qt::QueuedConnection);
}

void SqliteConversationRepository::usageForConversation(QString conversationId) {
  QMetaObject::invokeMethod(
      worker_,
      [worker = worker_, conversationId = std::move(conversationId)] { worker->usageForConversation(conversationId); },
      Qt::QueuedConnection);
}

}  // namespace holonight_persistence
