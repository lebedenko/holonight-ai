#include "credentials/fake_credential_store.h"
#include "holonight_application/provider_runtime_coordinator.h"

#include <gtest/gtest.h>
#include <utility>

namespace holonight_application {
namespace {

using holonight_credentials::FakeCredentialStore;

class CountingCredentialStore final : public FakeCredentialStore {
 public:
  using FakeCredentialStore::FakeCredentialStore;

  void retrieve(QString providerId) override {
    ++retrieve_count_;
    FakeCredentialStore::retrieve(std::move(providerId));
  }

  [[nodiscard]] int retrieveCount() const { return retrieve_count_; }

 private:
  int retrieve_count_ = 0;
};

class UnavailableCredentialStore final : public FakeCredentialStore {
 public:
  [[nodiscard]] bool isAvailable() const override { return false; }
};

ProviderRuntimeOperations operations(int* refresh_count, QString* credential = nullptr) {
  static const std::vector<holonight_domain::ModelId> kNoModels;
  return {
      .set_credential =
          [credential](const QString& value) {
            if (credential != nullptr) {
              *credential = value;
            }
          },
      .refresh =
          [refresh_count](const auto& success, const auto&) {
            ++*refresh_count;
            success();
          },
      .models = []() -> const std::vector<holonight_domain::ModelId>& { return kNoModels; },
      .persist_models = [] {},
  };
}

TEST(ProviderRuntimeCoordinator, RequiredCredentialIsAppliedBeforeRefresh) {
  FakeCredentialStore store;
  store.store(QStringLiteral("google"), QStringLiteral("secret"));
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  QString credential;
  coordinator.registerProvider(QStringLiteral("google"), QStringLiteral("Google"), CredentialPolicy::Required,
                               operations(&refreshCount, &credential));

  coordinator.prepare(QStringLiteral("google"));

  EXPECT_TRUE(coordinator.isReady(QStringLiteral("google")));
  EXPECT_EQ(credential, QStringLiteral("secret"));
  EXPECT_EQ(refreshCount, 1);
}

TEST(ProviderRuntimeCoordinator, MissingRequiredCredentialBlocksProvider) {
  FakeCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("openai"), QStringLiteral("OpenAI"), CredentialPolicy::Required,
                               operations(&refreshCount));

  coordinator.prepare(QStringLiteral("openai"));

  EXPECT_EQ(coordinator.readiness(QStringLiteral("openai")), ProviderReadiness::MissingCredential);
  EXPECT_EQ(refreshCount, 0);
  EXPECT_TRUE(coordinator.statusMessage(QStringLiteral("openai")).contains(QStringLiteral("Settings")));
}

TEST(ProviderRuntimeCoordinator, UnavailableCredentialStoreKeepsProviderVisibleButNotReady) {
  UnavailableCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("work-openai"), QStringLiteral("Work OpenAI"), CredentialPolicy::Required,
                               operations(&refreshCount));

  coordinator.prepare(QStringLiteral("work-openai"));

  EXPECT_EQ(coordinator.readiness(QStringLiteral("work-openai")), ProviderReadiness::Unavailable);
  EXPECT_FALSE(coordinator.isReady(QStringLiteral("work-openai")));
  EXPECT_EQ(refreshCount, 0);
  EXPECT_TRUE(coordinator.statusMessage(QStringLiteral("work-openai")).contains(QStringLiteral("unavailable")));
}

TEST(ProviderRuntimeCoordinator, StoredCredentialImmediatelyUnblocksPreparedProvider) {
  FakeCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  QString credential;
  coordinator.registerProvider(QStringLiteral("google"), QStringLiteral("Google"), CredentialPolicy::Required,
                               operations(&refreshCount, &credential));
  coordinator.prepare(QStringLiteral("google"));
  ASSERT_FALSE(coordinator.isReady(QStringLiteral("google")));

  store.store(QStringLiteral("google"), QStringLiteral("new-secret"));

  EXPECT_TRUE(coordinator.isReady(QStringLiteral("google")));
  EXPECT_EQ(credential, QStringLiteral("new-secret"));
  EXPECT_EQ(refreshCount, 1);
}

TEST(ProviderRuntimeCoordinator, OptionalCredentialDoesNotBlockLocalProvider) {
  FakeCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("ollama"), QStringLiteral("Ollama"), CredentialPolicy::Optional,
                               operations(&refreshCount));

  coordinator.prepare(QStringLiteral("ollama"));

  EXPECT_TRUE(coordinator.isReady(QStringLiteral("ollama")));
  EXPECT_EQ(refreshCount, 1);
}

TEST(ProviderRuntimeCoordinator, AutomaticRefreshRunsOnlyOnceButForceRefreshRunsAgain) {
  FakeCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("ollama"), QStringLiteral("Ollama"), CredentialPolicy::Optional,
                               operations(&refreshCount));

  coordinator.prepare(QStringLiteral("ollama"));
  coordinator.prepare(QStringLiteral("ollama"));
  EXPECT_EQ(refreshCount, 1);

  coordinator.forceRefresh(QStringLiteral("ollama"));
  EXPECT_EQ(refreshCount, 2);
}

TEST(ProviderRuntimeCoordinator, RepeatedPreparationDoesNotRetrieveCredentialAgain) {
  CountingCredentialStore store;
  store.store(QStringLiteral("openai"), QStringLiteral("secret"));
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("openai"), QStringLiteral("OpenAI"), CredentialPolicy::Required,
                               operations(&refreshCount));

  coordinator.prepare(QStringLiteral("openai"));
  coordinator.prepare(QStringLiteral("openai"));

  EXPECT_EQ(store.retrieveCount(), 1);
  EXPECT_EQ(refreshCount, 1);
}

TEST(ProviderRuntimeCoordinator, DisabledProviderDoesNotResolveCredentialsOrProbe) {
  CountingCredentialStore store;
  store.store(QStringLiteral("work-openai"), QStringLiteral("secret"));
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("work-openai"), QStringLiteral("Work OpenAI"), CredentialPolicy::Required,
                               operations(&refreshCount));

  coordinator.setEnabled(QStringLiteral("work-openai"), false);
  coordinator.prepare(QStringLiteral("work-openai"));

  EXPECT_FALSE(coordinator.isEnabled(QStringLiteral("work-openai")));
  EXPECT_EQ(store.retrieveCount(), 0);
  EXPECT_EQ(refreshCount, 0);
  EXPECT_EQ(coordinator.readiness(QStringLiteral("work-openai")), ProviderReadiness::Unresolved);
  EXPECT_TRUE(coordinator.statusMessage(QStringLiteral("work-openai")).isEmpty());
}

TEST(ProviderRuntimeCoordinator, ReenableStartsPreparationForThatInstance) {
  CountingCredentialStore store;
  store.store(QStringLiteral("personal-openai"), QStringLiteral("secret"));
  ProviderRuntimeCoordinator coordinator(&store);
  int refreshCount = 0;
  coordinator.registerProvider(QStringLiteral("personal-openai"), QStringLiteral("Personal OpenAI"),
                               CredentialPolicy::Required, operations(&refreshCount));
  coordinator.setEnabled(QStringLiteral("personal-openai"), false);

  coordinator.setEnabled(QStringLiteral("personal-openai"), true);

  EXPECT_TRUE(coordinator.isReady(QStringLiteral("personal-openai")));
  EXPECT_EQ(store.retrieveCount(), 1);
  EXPECT_EQ(refreshCount, 1);
}

TEST(ProviderRuntimeCoordinator, LateRefreshCompletionAfterDisableKeepsNeutralState) {
  FakeCredentialStore store;
  ProviderRuntimeCoordinator coordinator(&store);
  std::function<void()> completeRefresh;
  static const std::vector<holonight_domain::ModelId> kNoModels;
  coordinator.registerProvider(
      QStringLiteral("local-one"), QStringLiteral("Local One"), CredentialPolicy::Optional,
      ProviderRuntimeOperations{
          .set_credential = [](const QString&) {},
          .refresh = [&completeRefresh](const auto& success, const auto&) { completeRefresh = success; },
          .models = []() -> const std::vector<holonight_domain::ModelId>& { return kNoModels; },
          .persist_models = [] {},
      });
  coordinator.prepare(QStringLiteral("local-one"));
  ASSERT_TRUE(completeRefresh);

  coordinator.setEnabled(QStringLiteral("local-one"), false);
  completeRefresh();

  EXPECT_EQ(coordinator.readiness(QStringLiteral("local-one")), ProviderReadiness::Unresolved);
  EXPECT_EQ(coordinator.refreshState(QStringLiteral("local-one")), ProviderRefreshState::NotStarted);
  EXPECT_TRUE(coordinator.statusMessage(QStringLiteral("local-one")).isEmpty());
}

}  // namespace
}  // namespace holonight_application
