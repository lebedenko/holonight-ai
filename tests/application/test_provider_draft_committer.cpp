#include "credentials/fake_credential_store.h"
#include "holonight_application/provider_draft_committer.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_instance_registry.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

using holonight_config::ConfigRepository;
using holonight_config::OllamaProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderState;
using holonight_config::ProviderType;

ProviderInstanceConfig instance(QString name = QStringLiteral("Local")) {
  return ProviderInstanceConfig{.id = QStringLiteral("ollama"),
                                .type = ProviderType::Ollama,
                                .display_name = std::move(name),
                                .enabled = true,
                                .settings = OllamaProviderConfig{}};
}

class RetryCredentialStore final : public holonight_credentials::CredentialStore {
 public:
  void store(QString provider_id, QString secret) override {
    if (fail_) {
      emit unavailable(QStringLiteral("locked"));
      return;
    }
    secret_ = std::move(secret);
    emit storeCompleted(std::move(provider_id));
  }
  void retrieve(QString provider_id) override { emit retrieveCompleted(std::move(provider_id), false, {}); }
  void remove(QString provider_id) override {
    secret_.clear();
    emit removeCompleted(std::move(provider_id));
  }
  [[nodiscard]] QStringList listConfiguredProviders() const override { return {}; }
  [[nodiscard]] bool hasCredential(const QString& /*providerId*/) const override { return !secret_.isEmpty(); }
  [[nodiscard]] bool isAvailable() const override { return !fail_; }

  void allowOperations() { fail_ = false; }
  [[nodiscard]] QString secret() const { return secret_; }

 private:
  bool fail_ = true;
  QString secret_;
};

TEST(ProviderDraftCommitter, FailedStateWriteLeavesRegistryAndDraftUncommitted) {
  QTemporaryDir temporary_dir;
  ASSERT_TRUE(temporary_dir.isValid());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(temporary_dir.path()));

  EXPECT_FALSE(committer.save(&draft));

  ASSERT_EQ(registry.savedState().instances.size(), 1U);
  EXPECT_EQ(registry.savedState().instances.front().display_name, QStringLiteral("Local"));
  EXPECT_TRUE(draft.dirty());
  EXPECT_FALSE(committer.error().isEmpty());
}

TEST(ProviderDraftCommitter, DurableWritePublishesCompleteConfigAndClearsDraft) {
  QTemporaryDir temporary_dir;
  ASSERT_TRUE(temporary_dir.isValid());
  const QString config_path = temporary_dir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry;
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftSession draft(std::nullopt, instance(QStringLiteral("New Local")));
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(config_path));
  QSignalSpy completed_spy(&committer, &ProviderDraftCommitter::saveCompleted);

  EXPECT_TRUE(committer.save(&draft));

  ASSERT_EQ(registry.savedState().instances.size(), 1U);
  EXPECT_EQ(registry.savedState().instances.front().display_name, QStringLiteral("New Local"));
  EXPECT_EQ(ConfigRepository(config_path).loadProviderState(), registry.savedState());
  EXPECT_FALSE(draft.dirty());
  EXPECT_EQ(completed_spy.count(), 1);
}

TEST(ProviderDraftCommitter, CredentialFailureKeepsOnlySecretIntentDirtyAndCanRetry) {
  QTemporaryDir temporary_dir;
  ASSERT_TRUE(temporary_dir.isValid());
  const QString config_path = temporary_dir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  RetryCredentialStore credentials;
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  draft.setCredential(QStringLiteral("secret"));
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(config_path));

  EXPECT_TRUE(committer.save(&draft));

  EXPECT_EQ(registry.savedState().instances.front().display_name, QStringLiteral("Renamed"));
  EXPECT_EQ(draft.original()->display_name, QStringLiteral("Renamed"));
  EXPECT_TRUE(draft.dirty());
  EXPECT_EQ(draft.credentialEdit(), ProviderDraftSession::CredentialEdit::Store);
  EXPECT_TRUE(committer.credentialRetryPending());
  EXPECT_TRUE(committer.error().contains(QStringLiteral("locked")));

  credentials.allowOperations();
  EXPECT_TRUE(committer.retryCredential());
  EXPECT_EQ(credentials.secret(), QStringLiteral("secret"));
  EXPECT_FALSE(committer.credentialRetryPending());
  EXPECT_TRUE(committer.error().isEmpty());
  EXPECT_FALSE(draft.dirty());
}

}  // namespace
}  // namespace holonight_application
