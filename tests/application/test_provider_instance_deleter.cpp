#include "credentials/fake_credential_store.h"
#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_instance_deleter.h"
#include "holonight_application/provider_instance_registry.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "providers/fake_http_client.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <memory>
#include <utility>

namespace holonight_application {
namespace {

using holonight_config::ConfigRepository;
using holonight_config::OpenAIProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderState;
using holonight_config::ProviderType;
using holonight_config::UtilityConfig;
using holonight_domain::ModelId;
using holonight_providers::FakeHttpClient;

ProviderInstanceConfig instance() {
  return ProviderInstanceConfig{.id = QStringLiteral("550e8400-e29b-41d4-a716-446655440000"),
                                .type = ProviderType::OpenAi,
                                .display_name = QStringLiteral("Work OpenAI"),
                                .enabled = true,
                                .settings = OpenAIProviderConfig{}};
}

ProviderInstanceConfig fallbackInstance() {
  return ProviderInstanceConfig{.id = QStringLiteral("550e8400-e29b-41d4-a716-446655440001"),
                                .type = ProviderType::OpenAi,
                                .display_name = QStringLiteral("Personal OpenAI"),
                                .enabled = true,
                                .settings = OpenAIProviderConfig{}};
}

void registerRuntime(ProviderRuntimeCoordinator& coordinator, const QString& id) {
  coordinator.registerProvider(id, QStringLiteral("Work OpenAI"), CredentialPolicy::Required,
                               ProviderRuntimeOperations{});
}

class FailingRemoveCredentialStore final : public holonight_credentials::CredentialStore {
 public:
  void store(QString provider_id, QString /*secret*/) override { emit storeCompleted(std::move(provider_id)); }
  void retrieve(QString provider_id) override { emit retrieveCompleted(std::move(provider_id), false, {}); }
  void remove(QString provider_id) override {
    if (fail_) {
      emit unavailable(QStringLiteral("locked"));
    } else {
      emit removeCompleted(std::move(provider_id));
    }
  }
  [[nodiscard]] QStringList listConfiguredProviders() const override { return {}; }
  [[nodiscard]] bool hasCredential(const QString& /*providerId*/) const override { return false; }
  [[nodiscard]] bool isAvailable() const override { return !fail_; }
  void allowOperations() { fail_ = false; }

 private:
  bool fail_ = true;
};

TEST(ProviderInstanceDeleter, RequiresConfirmationWithoutChangingState) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, ConfigRepository(configPath));

  EXPECT_FALSE(deleter.remove(instance().id, false));

  EXPECT_EQ(registry.savedState().instances.size(), 1U);
  EXPECT_TRUE(router.contains(instance().id));
  EXPECT_TRUE(runtime.contains(instance().id));
  EXPECT_TRUE(deleter.error().contains(QStringLiteral("confirmation")));
}

TEST(ProviderInstanceDeleter, ActiveStreamBlocksDeletionBeforePersistence) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(ProviderState{.instances = {instance()}}).has_value());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  auto http = std::make_shared<FakeHttpClient>();
  ASSERT_TRUE(router.add(instance(), http));
  ASSERT_NE(router.sendChat(ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}, {},
                            [](const auto&) {}),
            nullptr);
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  EXPECT_FALSE(deleter.remove(instance().id, true));

  EXPECT_EQ(repository.loadProviderState().instances.size(), 1U);
  EXPECT_TRUE(router.contains(instance().id));
  EXPECT_TRUE(deleter.error().contains(QStringLiteral("streaming")));
}

TEST(ProviderInstanceDeleter, DurableDeletionCreatesTombstoneClearsUtilityAndRuntimeState) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(ProviderState{.instances = {instance()}}).has_value());
  ASSERT_TRUE(
      repository
          .saveUtilityConfig(UtilityConfig{
              .default_utility_model = ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}})
          .has_value());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ASSERT_TRUE(registry.selectSettingsInstance(instance().id));
  registry.setRuntimeModels(instance().id,
                            {ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}});
  registry.setRuntimeAvailable(instance().id, true);
  ASSERT_TRUE(registry.selectChatModel(ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}));
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  credentials.store(instance().id, QStringLiteral("secret"));
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  ASSERT_TRUE(deleter.remove(instance().id, true));

  const ProviderState persisted = repository.loadProviderState();
  EXPECT_TRUE(persisted.instances.empty());
  ASSERT_EQ(persisted.tombstones.size(), 1U);
  EXPECT_EQ(persisted.tombstones.front().instance_id, instance().id);
  EXPECT_EQ(persisted.tombstones.front().type, ProviderType::OpenAi);
  EXPECT_EQ(persisted.tombstones.front().last_display_name, QStringLiteral("Work OpenAI"));
  EXPECT_FALSE(repository.loadUtilityConfig().default_utility_model.has_value());
  EXPECT_TRUE(registry.selectedSettingsInstanceId().isEmpty());
  EXPECT_FALSE(registry.selectedChatModel().has_value());
  EXPECT_FALSE(router.contains(instance().id));
  EXPECT_FALSE(runtime.contains(instance().id));
  EXPECT_FALSE(credentials.hasCredential(instance().id));
}

// T-030 (REQ-F-017/REQ-NF-003): deleting an instance referenced only by chat_title_model_override
// must clear that field too, not just default_utility_model.
TEST(ProviderInstanceDeleter, DurableDeletionClearsChatTitleModelOverride) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(ProviderState{.instances = {instance()}}).has_value());
  ASSERT_TRUE(
      repository
          .saveUtilityConfig(UtilityConfig{
              .chat_title_model_override = ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}})
          .has_value());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  ASSERT_TRUE(deleter.remove(instance().id, true));

  EXPECT_FALSE(repository.loadUtilityConfig().chat_title_model_override.has_value());
}

// T-030 (REQ-F-017/REQ-NF-003): an instance referenced by BOTH default_utility_model and
// chat_title_model_override must have both fields cleared in the single atomic write.
TEST(ProviderInstanceDeleter, DurableDeletionClearsBothUtilityFieldsWhenBothReferenceDeletedInstance) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(ProviderState{.instances = {instance()}}).has_value());
  ASSERT_TRUE(
      repository
          .saveUtilityConfig(UtilityConfig{
              .default_utility_model = ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")},
              .chat_title_model_override = ModelId{.provider_id = instance().id, .model_name = QStringLiteral("gpt")}})
          .has_value());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  ASSERT_TRUE(deleter.remove(instance().id, true));

  const UtilityConfig persisted = repository.loadUtilityConfig();
  EXPECT_FALSE(persisted.default_utility_model.has_value());
  EXPECT_FALSE(persisted.chat_title_model_override.has_value());
}

// T-030 (REQ-F-017/REQ-NF-003): deleting an instance that chat_title_model_override does NOT
// reference must leave that field untouched — proves the clear is selective, not blanket.
TEST(ProviderInstanceDeleter, DurableDeletionLeavesUnrelatedChatTitleModelOverrideUntouched) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(ProviderState{.instances = {instance(), fallbackInstance()}}).has_value());
  const ModelId keptOverride{.provider_id = fallbackInstance().id, .model_name = QStringLiteral("gpt-personal")};
  ASSERT_TRUE(repository.saveUtilityConfig(UtilityConfig{.chat_title_model_override = keptOverride}).has_value());
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance(), fallbackInstance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  ASSERT_TRUE(router.add(fallbackInstance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  registerRuntime(runtime, fallbackInstance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  ASSERT_TRUE(deleter.remove(instance().id, true));

  EXPECT_EQ(repository.loadUtilityConfig().chat_title_model_override, keptOverride);
}

TEST(ProviderInstanceDeleter, DeletingSelectedInstanceFallsBackToSurvivingInstanceModel) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  const ProviderState state{.instances = {instance(), fallbackInstance()}};
  ConfigRepository repository(configPath);
  ASSERT_TRUE(repository.saveProviderState(state).has_value());
  ProviderInstanceRegistry registry(state);
  const ModelId selected{.provider_id = instance().id, .model_name = QStringLiteral("gpt-work")};
  const ModelId fallback{.provider_id = fallbackInstance().id, .model_name = QStringLiteral("gpt-personal")};
  registry.setRuntimeModels(instance().id, {selected});
  registry.setRuntimeModels(fallbackInstance().id, {fallback});
  registry.setRuntimeAvailable(instance().id, true);
  registry.setRuntimeAvailable(fallbackInstance().id, true);
  ASSERT_TRUE(registry.selectChatModel(selected));
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  ASSERT_TRUE(router.add(fallbackInstance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  registerRuntime(runtime, fallbackInstance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, repository);

  ASSERT_TRUE(deleter.remove(instance().id, true));

  EXPECT_EQ(registry.selectedChatModel(), fallback);
  EXPECT_FALSE(router.contains(instance().id));
  EXPECT_TRUE(router.contains(fallbackInstance().id));
  ASSERT_EQ(repository.loadProviderState().instances.size(), 1U);
  EXPECT_EQ(repository.loadProviderState().instances.front().id, fallbackInstance().id);
}

TEST(ProviderInstanceDeleter, WriteFailureLeavesAllLiveStateUnchanged) {
  QTemporaryDir temporaryDir;
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, ConfigRepository(temporaryDir.path()));

  EXPECT_FALSE(deleter.remove(instance().id, true));

  EXPECT_EQ(registry.savedState().instances.size(), 1U);
  EXPECT_TRUE(registry.savedState().tombstones.empty());
  EXPECT_TRUE(router.contains(instance().id));
  EXPECT_TRUE(runtime.contains(instance().id));
}

TEST(ProviderInstanceDeleter, CredentialFailureIsRetryableWithoutRestoringDeletedConfig) {
  QTemporaryDir temporaryDir;
  const QString configPath = temporaryDir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ProviderAdapterRouter router;
  ASSERT_TRUE(router.add(instance(), std::make_shared<FakeHttpClient>()));
  FailingRemoveCredentialStore credentials;
  ProviderRuntimeCoordinator runtime(&credentials);
  registerRuntime(runtime, instance().id);
  ProviderInstanceDeleter deleter(&registry, &router, &runtime, &credentials, ConfigRepository(configPath));

  ASSERT_TRUE(deleter.remove(instance().id, true));

  EXPECT_TRUE(registry.savedState().instances.empty());
  EXPECT_TRUE(deleter.credentialRetryPending());
  EXPECT_TRUE(deleter.error().contains(QStringLiteral("locked")));
  credentials.allowOperations();
  EXPECT_TRUE(deleter.retryCredentialCleanup());
  EXPECT_FALSE(deleter.credentialRetryPending());
  EXPECT_TRUE(deleter.error().isEmpty());
  EXPECT_TRUE(ConfigRepository(configPath).loadProviderState().instances.empty());
}

}  // namespace
}  // namespace holonight_application
