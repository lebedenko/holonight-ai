#include "holonight_credentials/detail/credential_store_worker.h"
#include "holonight_credentials/secret_service_credential_store.h"

#include <QSignalSpy>
#include <QThread>

#include <chrono>
#include <gtest/gtest.h>

namespace holonight_credentials {
namespace {

TEST(CredentialStoreWorkerThreadingTest, SlotsExecuteOnWorkerThread) {
  QThread workerThread;
  auto* worker = new detail::CredentialStoreWorker();
  worker->moveToThread(&workerThread);
  workerThread.start();

  QThread* observed = nullptr;
  QObject::connect(
      worker, &detail::CredentialStoreWorker::knownProviderIdsLoaded, worker,
      [&] { observed = QThread::currentThread(); }, Qt::DirectConnection);
  QObject::connect(
      worker, &detail::CredentialStoreWorker::unavailable, worker, [&] { observed = QThread::currentThread(); },
      Qt::DirectConnection);

  QSignalSpy loadedSpy(worker, &detail::CredentialStoreWorker::knownProviderIdsLoaded);
  QSignalSpy unavailableSpy(worker, &detail::CredentialStoreWorker::unavailable);
  QMetaObject::invokeMethod(worker, [worker] { worker->loadKnownProviderIds(); }, Qt::QueuedConnection);

  ASSERT_TRUE(loadedSpy.wait(2000) || !unavailableSpy.isEmpty());

  EXPECT_EQ(observed, &workerThread);
  EXPECT_NE(observed, QThread::currentThread());

  workerThread.quit();
  workerThread.wait();
}

TEST(SecretServiceCredentialStoreThreadingTest, StoreReturnsImmediately) {
  SecretServiceCredentialStore store;
  const QString providerId = QStringLiteral("test-provider-threading");

  QSignalSpy storeSpy(&store, &CredentialStore::storeCompleted);
  const auto start = std::chrono::steady_clock::now();
  store.store(providerId, QStringLiteral("test-secret"));
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(50));

  // If this call reached a real, reachable Secret Service, wait for it to actually complete and
  // clean up afterward — the queued store() above may or may not have run before this point (that
  // race is exactly what the elapsed-time assertion above is measuring), but this test must never
  // leave a stray "test-provider-threading" secret behind in the developer's real keyring
  // regardless of how that race resolves.
  if (storeSpy.wait(2000)) {
    QSignalSpy removeSpy(&store, &CredentialStore::removeCompleted);
    store.remove(providerId);
    removeSpy.wait(2000);
  }
}

TEST(SecretServiceCredentialStoreIntegrationTest, StoreAndRetrieveWithRealService) {
  SecretServiceCredentialStore store;

  QSignalSpy unavailableSpy(&store, &CredentialStore::unavailable);
  // Give the constructor's startup enumeration a brief window to complete or fail.
  unavailableSpy.wait(500);

  if (!unavailableSpy.isEmpty() || !store.isAvailable()) {
    GTEST_SKIP() << "Secret Service not reachable in this environment";
  }

  const QString providerId = QStringLiteral("test-provider-integration-xxx");
  const QString secret = QStringLiteral("test-secret-integration-xxx");

  QSignalSpy storeSpy(&store, &CredentialStore::storeCompleted);
  store.store(providerId, secret);
  if (!storeSpy.wait(2000) || !store.isAvailable()) {
    GTEST_SKIP() << "Secret Service became unavailable during store()";
  }

  QSignalSpy retrieveSpy(&store, &CredentialStore::retrieveCompleted);
  store.retrieve(providerId);
  ASSERT_TRUE(retrieveSpy.wait(2000));
  EXPECT_TRUE(retrieveSpy.at(0).at(1).toBool());
  EXPECT_EQ(retrieveSpy.at(0).at(2).toString(), secret);

  QSignalSpy removeSpy(&store, &CredentialStore::removeCompleted);
  store.remove(providerId);
  ASSERT_TRUE(removeSpy.wait(2000));

  QSignalSpy retrieveAfterRemoveSpy(&store, &CredentialStore::retrieveCompleted);
  store.retrieve(providerId);
  ASSERT_TRUE(retrieveAfterRemoveSpy.wait(2000));
  EXPECT_FALSE(retrieveAfterRemoveSpy.at(0).at(1).toBool());
}

}  // namespace
}  // namespace holonight_credentials
