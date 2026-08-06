#include "fake_credential_store.h"

#include <QSignalSpy>

#include <gtest/gtest.h>

namespace holonight_credentials {
namespace {

TEST(FakeCredentialStoreTest, StoreCreatesSecret) {
  FakeCredentialStore store;
  QSignalSpy spy(&store, &CredentialStore::storeCompleted);

  store.store(QStringLiteral("openai"), QStringLiteral("sk-123"));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("openai"));
  EXPECT_TRUE(store.hasCredential(QStringLiteral("openai")));
}

TEST(FakeCredentialStoreTest, StoreOverwritesExisting) {
  FakeCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("sk-old"));
  store.store(QStringLiteral("openai"), QStringLiteral("sk-new"));

  QSignalSpy spy(&store, &CredentialStore::retrieveCompleted);
  store.retrieve(QStringLiteral("openai"));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.at(0).at(1).toBool());
  EXPECT_EQ(spy.at(0).at(2).toString(), QStringLiteral("sk-new"));
}

TEST(FakeCredentialStoreTest, RetrieveNotFound) {
  FakeCredentialStore store;
  QSignalSpy spy(&store, &CredentialStore::retrieveCompleted);

  store.retrieve(QStringLiteral("nonexistent"));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("nonexistent"));
  EXPECT_FALSE(spy.at(0).at(1).toBool());
  EXPECT_EQ(spy.at(0).at(2).toString(), QString());
}

TEST(FakeCredentialStoreTest, RetrieveFound) {
  FakeCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("sk-test"));

  QSignalSpy spy(&store, &CredentialStore::retrieveCompleted);
  store.retrieve(QStringLiteral("openai"));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("openai"));
  EXPECT_TRUE(spy.at(0).at(1).toBool());
  EXPECT_EQ(spy.at(0).at(2).toString(), QStringLiteral("sk-test"));
}

TEST(FakeCredentialStoreTest, RemoveNonexistent) {
  FakeCredentialStore store;
  QSignalSpy spy(&store, &CredentialStore::removeCompleted);

  store.remove(QStringLiteral("nonexistent"));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("nonexistent"));
}

TEST(FakeCredentialStoreTest, RemoveExisting) {
  FakeCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("sk-test"));

  QSignalSpy removeSpy(&store, &CredentialStore::removeCompleted);
  store.remove(QStringLiteral("openai"));
  ASSERT_EQ(removeSpy.count(), 1);

  QSignalSpy retrieveSpy(&store, &CredentialStore::retrieveCompleted);
  store.retrieve(QStringLiteral("openai"));
  ASSERT_EQ(retrieveSpy.count(), 1);
  EXPECT_FALSE(retrieveSpy.at(0).at(1).toBool());
}

TEST(FakeCredentialStoreTest, ListConfiguredProvidersEmpty) {
  FakeCredentialStore store;
  EXPECT_TRUE(store.listConfiguredProviders().isEmpty());
}

TEST(FakeCredentialStoreTest, ListConfiguredProvidersPopulated) {
  FakeCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("sk-1"));
  store.store(QStringLiteral("anthropic"), QStringLiteral("sk-2"));

  const QStringList providers = store.listConfiguredProviders();
  EXPECT_TRUE(providers.contains(QStringLiteral("openai")));
  EXPECT_TRUE(providers.contains(QStringLiteral("anthropic")));
  EXPECT_EQ(providers.size(), 2);
}

TEST(FakeCredentialStoreTest, HasCredentialAfterStoreAndRemove) {
  FakeCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("sk-1"));
  EXPECT_TRUE(store.hasCredential(QStringLiteral("openai")));

  store.remove(QStringLiteral("openai"));
  EXPECT_FALSE(store.hasCredential(QStringLiteral("openai")));
}

TEST(FakeCredentialStoreTest, AlwaysAvailable) {
  FakeCredentialStore store;
  EXPECT_TRUE(store.isAvailable());

  store.store(QStringLiteral("openai"), QStringLiteral("sk-1"));
  store.retrieve(QStringLiteral("openai"));
  store.remove(QStringLiteral("openai"));

  EXPECT_TRUE(store.isAvailable());
}

TEST(FakeCredentialStoreTest, NeverEmitsUnavailable) {
  FakeCredentialStore store;
  QSignalSpy spy(&store, &CredentialStore::unavailable);

  store.store(QStringLiteral("openai"), QStringLiteral("sk-1"));
  store.retrieve(QStringLiteral("openai"));
  store.remove(QStringLiteral("openai"));

  EXPECT_EQ(spy.count(), 0);
}

}  // namespace
}  // namespace holonight_credentials
