#include "holonight_persistence/detail/conversation_repository_worker.h"
#include "test_support.h"

#include <QSignalSpy>
#include <QThread>

#include <gtest/gtest.h>

namespace holonight_persistence::detail {
namespace {

TEST(ConversationRepositoryWorkerThreading, SlotsExecuteOnWorkerThread) {
  QThread workerThread;
  auto* worker = new ConversationRepositoryWorker(QStringLiteral(":memory:"), uniqueTestConnectionName());
  worker->moveToThread(&workerThread);
  workerThread.start();

  QThread* observed = nullptr;
  QObject::connect(
      worker, &ConversationRepositoryWorker::initialized, worker, [&] { observed = QThread::currentThread(); },
      Qt::DirectConnection);

  QSignalSpy initializedSpy(worker, &ConversationRepositoryWorker::initialized);
  QMetaObject::invokeMethod(worker, [worker] { worker->openAndMigrate(); }, Qt::QueuedConnection);
  ASSERT_TRUE(initializedSpy.wait(1000));

  EXPECT_EQ(observed, &workerThread);
  EXPECT_NE(observed, QThread::currentThread());

  workerThread.quit();
  workerThread.wait();
}

TEST(ConversationRepositoryWorkerThreading, EmitsUnavailableOnUnopenableDatabase) {
  QThread workerThread;
  auto* worker = new ConversationRepositoryWorker(QStringLiteral("/nonexistent/path/conversations.db"),
                                                  uniqueTestConnectionName());
  worker->moveToThread(&workerThread);
  workerThread.start();

  QSignalSpy unavailableSpy(worker, &ConversationRepositoryWorker::unavailable);
  QMetaObject::invokeMethod(worker, [worker] { worker->openAndMigrate(); }, Qt::QueuedConnection);
  ASSERT_TRUE(unavailableSpy.wait(1000));

  workerThread.quit();
  workerThread.wait();
}

}  // namespace
}  // namespace holonight_persistence::detail
