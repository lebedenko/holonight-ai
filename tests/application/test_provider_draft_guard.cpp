#include "credentials/fake_credential_store.h"
#include "holonight_application/provider_draft_committer.h"
#include "holonight_application/provider_draft_guard.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_instance_registry.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <tuple>

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

enum class NavigationPath { ProviderSelection, SettingsSection, WindowClose };
enum class NavigationDecision { Save, Discard, Cancel };

class ProviderDraftGuardNavigationTest : public testing::TestWithParam<std::tuple<NavigationPath, NavigationDecision>> {
};

TEST_P(ProviderDraftGuardNavigationTest, AppliesDecisionBeforeEveryNavigationPath) {
  QTemporaryDir temporary_dir;
  const QString config_path = temporary_dir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ASSERT_TRUE(registry.selectSettingsInstance(QStringLiteral("ollama")));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(config_path));
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  const auto [path, decision] = GetParam();
  std::optional<NavigationPath> completed_path;

  guard.requestNavigation([&] { completed_path = path; });
  ASSERT_TRUE(guard.navigationPending());

  switch (decision) {
    case NavigationDecision::Save:
      ASSERT_TRUE(guard.saveAndContinue());
      EXPECT_EQ(registry.savedState().instances.front().display_name, QStringLiteral("Renamed"));
      EXPECT_EQ(ConfigRepository(config_path).loadProviderState(), registry.savedState());
      EXPECT_FALSE(draft.dirty());
      EXPECT_EQ(completed_path, path);
      break;
    case NavigationDecision::Discard:
      ASSERT_TRUE(guard.discardAndContinue());
      EXPECT_EQ(draft.editable(), instance());
      EXPECT_FALSE(draft.dirty());
      EXPECT_EQ(completed_path, path);
      break;
    case NavigationDecision::Cancel:
      guard.cancelNavigation();
      EXPECT_TRUE(draft.dirty());
      EXPECT_FALSE(completed_path.has_value());
      EXPECT_EQ(registry.selectedSettingsInstanceId(), QStringLiteral("ollama"));
      break;
  }
  EXPECT_FALSE(guard.navigationPending());
}

INSTANTIATE_TEST_SUITE_P(EveryPathAndDecision, ProviderDraftGuardNavigationTest,
                         testing::Combine(testing::Values(NavigationPath::ProviderSelection,
                                                          NavigationPath::SettingsSection, NavigationPath::WindowClose),
                                          testing::Values(NavigationDecision::Save, NavigationDecision::Discard,
                                                          NavigationDecision::Cancel)));

TEST(ProviderDraftGuard, CleanExistingDraftNavigatesImmediately) {
  QTemporaryDir temporary_dir;
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials,
                                   ConfigRepository(temporary_dir.filePath(QStringLiteral("config.json"))));
  ProviderDraftSession draft(instance(), instance());
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  bool navigated = false;

  guard.requestNavigation([&navigated] { navigated = true; });

  EXPECT_TRUE(navigated);
  EXPECT_FALSE(guard.navigationPending());
}

TEST(ProviderDraftGuard, CancelKeepsDirtyDraftAndSelectionInPlace) {
  QTemporaryDir temporary_dir;
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ASSERT_TRUE(registry.selectSettingsInstance(QStringLiteral("ollama")));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials,
                                   ConfigRepository(temporary_dir.filePath(QStringLiteral("config.json"))));
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  QSignalSpy prompt_spy(&guard, &ProviderDraftGuard::confirmationRequested);
  bool navigated = false;

  guard.requestNavigation([&navigated] { navigated = true; });
  guard.cancelNavigation();

  EXPECT_EQ(prompt_spy.count(), 1);
  EXPECT_FALSE(navigated);
  EXPECT_TRUE(draft.dirty());
  EXPECT_EQ(registry.selectedSettingsInstanceId(), QStringLiteral("ollama"));
  EXPECT_FALSE(guard.navigationPending());
}

TEST(ProviderDraftGuard, FailedSaveKeepsPendingNavigationAndCurrentDraft) {
  QTemporaryDir temporary_dir;
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  ASSERT_TRUE(registry.selectSettingsInstance(QStringLiteral("ollama")));
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(temporary_dir.path()));
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  bool navigated = false;
  guard.requestNavigation([&navigated] { navigated = true; });

  EXPECT_FALSE(guard.saveAndContinue());

  EXPECT_FALSE(navigated);
  EXPECT_TRUE(guard.navigationPending());
  EXPECT_TRUE(draft.dirty());
  EXPECT_EQ(registry.selectedSettingsInstanceId(), QStringLiteral("ollama"));
  EXPECT_FALSE(committer.error().isEmpty());
}

TEST(ProviderDraftGuard, SuccessfulSaveCommitsBeforeContinuingNavigation) {
  QTemporaryDir temporary_dir;
  const QString config_path = temporary_dir.filePath(QStringLiteral("config.json"));
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials, ConfigRepository(config_path));
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  bool observed_committed_state = false;
  guard.requestNavigation([&] {
    observed_committed_state = registry.savedState().instances.front().display_name == QStringLiteral("Renamed");
  });

  EXPECT_TRUE(guard.saveAndContinue());

  EXPECT_TRUE(observed_committed_state);
  EXPECT_FALSE(draft.dirty());
  EXPECT_FALSE(guard.navigationPending());
}

TEST(ProviderDraftGuard, DiscardRestoresExistingDraftBeforeContinuingNavigation) {
  QTemporaryDir temporary_dir;
  ProviderInstanceRegistry registry(ProviderState{.instances = {instance()}});
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials,
                                   ConfigRepository(temporary_dir.filePath(QStringLiteral("config.json"))));
  ProviderDraftSession draft(instance(), instance(QStringLiteral("Renamed")));
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  bool observed_restored_draft = false;
  guard.requestNavigation([&] { observed_restored_draft = draft.editable() == instance(); });

  EXPECT_TRUE(guard.discardAndContinue());

  EXPECT_TRUE(observed_restored_draft);
  EXPECT_FALSE(draft.dirty());
}

TEST(ProviderDraftGuard, UnsavedAdditionIsGuardedAndDiscardRemovesItBeforeNavigation) {
  QTemporaryDir temporary_dir;
  ProviderInstanceRegistry registry;
  const auto addition = registry.addDraft(ProviderType::Ollama);
  ASSERT_TRUE(addition.has_value());
  holonight_credentials::FakeCredentialStore credentials;
  ProviderDraftCommitter committer(&registry, &credentials,
                                   ConfigRepository(temporary_dir.filePath(QStringLiteral("config.json"))));
  ProviderDraftSession draft(std::nullopt, *addition);
  ProviderDraftGuard guard(&registry, &committer);
  guard.setDraftSession(&draft);
  bool observed_removed_addition = false;
  guard.requestNavigation([&] {
    observed_removed_addition = registry.instances()->rowCount() == 0 &&
                                registry.selectedSettingsInstanceId().isEmpty() && guard.draftSession() == nullptr;
  });

  EXPECT_TRUE(guard.navigationPending());
  EXPECT_TRUE(guard.discardAndContinue());

  EXPECT_TRUE(observed_removed_addition);
  EXPECT_FALSE(guard.navigationPending());
}

}  // namespace
}  // namespace holonight_application
