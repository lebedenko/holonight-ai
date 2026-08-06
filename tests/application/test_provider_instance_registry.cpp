#include "holonight_application/provider_instance_registry.h"

#include <QSignalSpy>
#include <QUuid>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

using holonight_config::OllamaProviderConfig;
using holonight_config::OpenAIProviderConfig;
using holonight_config::ProviderInstanceConfig;
using holonight_config::ProviderState;
using holonight_config::ProviderType;
using holonight_domain::ModelId;

ProviderInstanceConfig instance(QString id, ProviderType type, QString name, bool enabled = true) {
  return ProviderInstanceConfig{
      .id = std::move(id),
      .type = type,
      .display_name = std::move(name),
      .enabled = enabled,
      .settings = type == ProviderType::OpenAi ? holonight_config::ProviderSettings{OpenAIProviderConfig{}}
                                               : holonight_config::ProviderSettings{OllamaProviderConfig{}},
  };
}

TEST(ProviderInstanceRegistry, LoadedInstancesPreserveOrderAndStartWithoutSelection) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("first"), ProviderType::Ollama, QStringLiteral("Local")),
                        instance(QStringLiteral("second"), ProviderType::OpenAi, QStringLiteral("Cloud"), false),
                    }});

  ASSERT_EQ(registry.instances()->rowCount(), 2);
  EXPECT_EQ(registry.instances()->data(registry.instances()->index(0), ProviderInstanceListModel::InstanceIdRole),
            QStringLiteral("first"));
  EXPECT_EQ(registry.instances()->data(registry.instances()->index(1), ProviderInstanceListModel::InstanceIdRole),
            QStringLiteral("second"));
  EXPECT_FALSE(
      registry.instances()->data(registry.instances()->index(1), ProviderInstanceListModel::EnabledRole).toBool());
  EXPECT_TRUE(registry.selectedSettingsInstanceId().isEmpty());
}

TEST(ProviderInstanceRegistry, ModelPublishesStableQmlFacingRoles) {
  ProviderInstanceRegistry registry;

  const auto roles = registry.instances()->roleNames();
  EXPECT_EQ(roles.value(ProviderInstanceListModel::InstanceIdRole), QByteArrayLiteral("instanceId"));
  EXPECT_EQ(roles.value(ProviderInstanceListModel::ProviderTypeRole), QByteArrayLiteral("providerType"));
  EXPECT_EQ(roles.value(ProviderInstanceListModel::DisplayNameRole), QByteArrayLiteral("displayName"));
  EXPECT_EQ(roles.value(ProviderInstanceListModel::EnabledRole), QByteArrayLiteral("enabled"));
  EXPECT_EQ(roles.value(ProviderInstanceListModel::DraftRole), QByteArrayLiteral("draft"));
}

TEST(ProviderInstanceRegistry, AddDraftCreatesUuidPrependsDefaultsAndSelectsIt) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("saved"), ProviderType::Ollama, QStringLiteral("Ollama")),
                    }});
  QSignalSpy insertedSpy(registry.instances(), &QAbstractItemModel::rowsInserted);
  QSignalSpy selectionSpy(&registry, &ProviderInstanceRegistry::selectedSettingsInstanceIdChanged);

  const auto draft = registry.addDraft(ProviderType::OpenAi);

  ASSERT_TRUE(draft.has_value());
  EXPECT_FALSE(QUuid::fromString(draft->id).isNull());
  EXPECT_EQ(draft->display_name, QStringLiteral("OpenAI"));
  EXPECT_TRUE(draft->enabled);
  EXPECT_TRUE(std::holds_alternative<OpenAIProviderConfig>(draft->settings));
  EXPECT_EQ(registry.instances()->data(registry.instances()->index(0), ProviderInstanceListModel::InstanceIdRole),
            draft->id);
  EXPECT_TRUE(
      registry.instances()->data(registry.instances()->index(0), ProviderInstanceListModel::DraftRole).toBool());
  EXPECT_EQ(registry.selectedSettingsInstanceId(), draft->id);
  EXPECT_EQ(insertedSpy.count(), 1);
  EXPECT_EQ(selectionSpy.count(), 1);
  EXPECT_EQ(registry.savedState().instances.size(), 1U);
}

TEST(ProviderInstanceRegistry, OnlyOneDraftCanExistAndCancelRemovesItWithoutSelectingFallback) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("saved"), ProviderType::Ollama, QStringLiteral("Ollama")),
                    }});
  ASSERT_TRUE(registry.addDraft(ProviderType::Google).has_value());
  EXPECT_FALSE(registry.addDraft(ProviderType::Anthropic).has_value());
  QSignalSpy removedSpy(registry.instances(), &QAbstractItemModel::rowsRemoved);

  EXPECT_TRUE(registry.cancelDraft());

  EXPECT_EQ(registry.instances()->rowCount(), 1);
  EXPECT_TRUE(registry.selectedSettingsInstanceId().isEmpty());
  EXPECT_EQ(removedSpy.count(), 1);
  EXPECT_FALSE(registry.cancelDraft());
}

TEST(ProviderInstanceRegistry, SelectionChangesOnlyForKnownInstancesOrExplicitClear) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("known"), ProviderType::Ollama, QStringLiteral("Ollama")),
                    }});
  QSignalSpy selectionSpy(&registry, &ProviderInstanceRegistry::selectedSettingsInstanceIdChanged);

  EXPECT_FALSE(registry.selectSettingsInstance(QStringLiteral("missing")));
  EXPECT_TRUE(registry.selectedSettingsInstanceId().isEmpty());
  EXPECT_TRUE(registry.selectSettingsInstance(QStringLiteral("known")));
  EXPECT_EQ(registry.selectedSettingsInstanceId(), QStringLiteral("known"));
  registry.clearSettingsSelection();
  EXPECT_TRUE(registry.selectedSettingsInstanceId().isEmpty());
  EXPECT_EQ(selectionSpy.count(), 2);
}

TEST(ProviderInstanceRegistry, NamesAreTrimmedUnicodeCaseFoldedAndGlobalAcrossTypes) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("one"), ProviderType::Ollama, QStringLiteral("  Київ  ")),
                        instance(QStringLiteral("two"), ProviderType::OpenAi, QStringLiteral("Ollama")),
                    }});

  EXPECT_FALSE(registry.isDisplayNameAvailable(QStringLiteral("КИЇВ")));
  EXPECT_FALSE(registry.isDisplayNameAvailable(QStringLiteral(" ollama ")));
  EXPECT_FALSE(registry.isDisplayNameAvailable(QStringLiteral("   ")));
  EXPECT_TRUE(registry.isDisplayNameAvailable(QStringLiteral("Anthropic")));
}

TEST(ProviderInstanceRegistry, LowestFreeNameUsesGapsRatherThanInstanceCount) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("one"), ProviderType::Ollama, QStringLiteral("Ollama")),
                        instance(QStringLiteral("three"), ProviderType::OpenAi, QStringLiteral("ollama 3")),
                        instance(QStringLiteral("other"), ProviderType::OpenAi, QStringLiteral("Work")),
                    }});

  EXPECT_EQ(registry.nextAvailableDisplayName(ProviderType::Ollama), QStringLiteral("Ollama 2"));
}

TEST(ProviderInstanceRegistry, UnavailableInstanceIsNotSelectedAndBecomesUsableWhenReady) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("work"), ProviderType::OpenAi, QStringLiteral("Work")),
                    }});
  const ModelId model{.provider_id = QStringLiteral("work"), .model_name = QStringLiteral("gpt")};
  registry.setRuntimeModels(QStringLiteral("work"), {model});

  EXPECT_FALSE(registry.selectChatModel(model));
  EXPECT_FALSE(registry.selectedChatModel().has_value());

  registry.setRuntimeAvailable(QStringLiteral("work"), true);

  EXPECT_EQ(registry.selectedChatModel(), model);
}

TEST(ProviderInstanceRegistry, DisableAndDeleteFallBackInSavedOrderThenClear) {
  ProviderInstanceRegistry registry(
      ProviderState{.instances = {
                        instance(QStringLiteral("first"), ProviderType::OpenAi, QStringLiteral("First")),
                        instance(QStringLiteral("second"), ProviderType::OpenAi, QStringLiteral("Second")),
                    }});
  const ModelId first{.provider_id = QStringLiteral("first"), .model_name = QStringLiteral("shared")};
  const ModelId second{.provider_id = QStringLiteral("second"), .model_name = QStringLiteral("shared")};
  registry.setRuntimeModels(first.provider_id, {first});
  registry.setRuntimeModels(second.provider_id, {second});
  registry.setRuntimeAvailable(first.provider_id, true);
  registry.setRuntimeAvailable(second.provider_id, true);
  ASSERT_TRUE(registry.selectChatModel(first));

  ASSERT_TRUE(registry.applyEnabledState(first.provider_id, false));
  EXPECT_EQ(registry.selectedChatModel(), second);

  ASSERT_TRUE(registry.applyRemoval(second.provider_id));
  EXPECT_FALSE(registry.selectedChatModel().has_value());
  EXPECT_EQ(registry.instances()->rowCount(), 1);
}

TEST(ProviderInstanceRegistry, HistoricalIdentityPrefersLiveThenTombstoneThenUnknown) {
  ProviderInstanceRegistry registry(ProviderState{
      .instances = {instance(QStringLiteral("live"), ProviderType::OpenAi, QStringLiteral("Renamed live"))},
      .tombstones = {{.instance_id = QStringLiteral("live"),
                      .type = ProviderType::Ollama,
                      .last_display_name = QStringLiteral("Stale name")},
                     {.instance_id = QStringLiteral("deleted"),
                      .type = ProviderType::Anthropic,
                      .last_display_name = QStringLiteral("Deleted work")}}});

  EXPECT_EQ(registry.historicalIdentity(QStringLiteral("live")),
            (HistoricalProviderIdentity{.instance_id = QStringLiteral("live"),
                                        .type = ProviderType::OpenAi,
                                        .display_name = QStringLiteral("Renamed live")}));
  EXPECT_EQ(registry.historicalIdentity(QStringLiteral("deleted")),
            (HistoricalProviderIdentity{.instance_id = QStringLiteral("deleted"),
                                        .type = ProviderType::Anthropic,
                                        .display_name = QStringLiteral("Deleted work"),
                                        .tombstone = true}));
  EXPECT_EQ(registry.historicalIdentity(QStringLiteral("foreign")),
            (HistoricalProviderIdentity{.instance_id = QStringLiteral("foreign"),
                                        .display_name = QStringLiteral("Unknown provider")}));
}

}  // namespace
}  // namespace holonight_application
