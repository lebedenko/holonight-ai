#include "holonight_config/config_repository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace holonight_config {
namespace {

QString scratchFilePath(const QTemporaryDir& dir, const QString& name = QStringLiteral("config.json")) {
  return dir.filePath(name);
}

TEST(ConfigRepository, CachedModelsRoundTripAndRemainScopedToProvider) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));

  ASSERT_TRUE(repository
                  .saveCachedModels(QStringLiteral("google"),
                                    {QStringLiteral("gemini-flash-latest"), QStringLiteral("gemini-pro-latest")})
                  .has_value());

  EXPECT_EQ(repository.loadCachedModels(QStringLiteral("google")),
            QStringList({QStringLiteral("gemini-flash-latest"), QStringLiteral("gemini-pro-latest")}));
  EXPECT_TRUE(repository.loadCachedModels(QStringLiteral("openai")).isEmpty());
}

TEST(ConfigRepository, SavingProviderStatePreservesCachedModels) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  ASSERT_TRUE(
      repository.saveCachedModels(QStringLiteral("google"), {QStringLiteral("gemini-flash-latest")}).has_value());

  ProviderState state;
  state.instances.push_back({.id = QStringLiteral("google"),
                             .type = ProviderType::Google,
                             .display_name = QStringLiteral("Google"),
                             .settings = GoogleProviderConfig{.default_model = QStringLiteral("gemini-flash-latest")}});
  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  EXPECT_EQ(repository.loadCachedModels(QStringLiteral("google")),
            QStringList({QStringLiteral("gemini-flash-latest")}));
  EXPECT_EQ(repository.loadProviderState(), state);
}

TEST(ConfigRepository, LoadMissingUtilityKeyReturnsUnsetDefault) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));

  const UtilityConfig config = repository.loadUtilityConfig();

  EXPECT_EQ(config, UtilityConfig{});
  EXPECT_FALSE(config.default_utility_model.has_value());
}

TEST(ConfigRepository, LoadNullUtilityKeyReturnsUnsetDefault) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"utility":null})");
  file.close();

  ConfigRepository repository(path);

  EXPECT_EQ(repository.loadUtilityConfig(), UtilityConfig{});
}

TEST(ConfigRepository, LoadMalformedUtilityModelReturnsUnsetDefault) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"utility":{"default_utility_model":"not an object"}})");
  file.close();

  ConfigRepository repository(path);

  EXPECT_EQ(repository.loadUtilityConfig(), UtilityConfig{});
}

TEST(ConfigRepository, LoadUtilityModelMissingModelNameReturnsUnsetDefault) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"utility":{"default_utility_model":{"provider_id":"openai"}}})");
  file.close();

  ConfigRepository repository(path);

  EXPECT_EQ(repository.loadUtilityConfig(), UtilityConfig{});
}

TEST(ConfigRepository, SaveThenLoadSetUtilityModelRoundTripsExactly) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig original;
  original.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("haiku")};

  ASSERT_TRUE(repository.saveUtilityConfig(original).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  EXPECT_EQ(loaded, original);
}

TEST(ConfigRepository, SaveThenLoadUnsetUtilityModelRoundTripsToDefault) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig set;
  set.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini")};
  ASSERT_TRUE(repository.saveUtilityConfig(set).has_value());

  ASSERT_TRUE(repository.saveUtilityConfig(UtilityConfig{}).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  EXPECT_EQ(loaded, UtilityConfig{});
  EXPECT_FALSE(loaded.default_utility_model.has_value());
}

TEST(ConfigRepository, SaveUtilityConfigPreservesProviderState) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  ProviderState state;
  state.instances.push_back({.id = QStringLiteral("ollama"),
                             .type = ProviderType::Ollama,
                             .display_name = QStringLiteral("Ollama"),
                             .settings = OllamaProviderConfig{.default_model = QStringLiteral("llama3.2")}});
  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  UtilityConfig utility;
  utility.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3.2")};
  ASSERT_TRUE(repository.saveUtilityConfig(utility).has_value());

  EXPECT_EQ(repository.loadProviderState(), state);
  EXPECT_EQ(repository.loadUtilityConfig(), utility);
}

// T-026 (REQ-F-008, REQ-NF-002): absent chat_title_generation_enabled must load as nullopt, not
// false — .value_or(true) at UtilityTaskRunner's call site is what actually implements "absent ⇒
// enabled", so the loader must not collapse the distinction itself.
TEST(ConfigRepository, LoadMissingChatTitleGenerationEnabledReturnsNullopt) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));

  const UtilityConfig config = repository.loadUtilityConfig();

  EXPECT_FALSE(config.chat_title_generation_enabled.has_value());
}

TEST(ConfigRepository, ChatTitleGenerationEnabledRoundTripsExactlyWhenSetFalse) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig original;
  original.chat_title_generation_enabled = false;

  ASSERT_TRUE(repository.saveUtilityConfig(original).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  ASSERT_TRUE(loaded.chat_title_generation_enabled.has_value());
  EXPECT_FALSE(*loaded.chat_title_generation_enabled);
}

TEST(ConfigRepository, ChatTitleGenerationEnabledRoundTripsExactlyWhenSetTrue) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig original;
  original.chat_title_generation_enabled = true;

  ASSERT_TRUE(repository.saveUtilityConfig(original).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  ASSERT_TRUE(loaded.chat_title_generation_enabled.has_value());
  EXPECT_TRUE(*loaded.chat_title_generation_enabled);
}

TEST(ConfigRepository, MalformedChatTitleGenerationEnabledReturnsNullopt) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"utility":{"chat_title_generation_enabled":"not a bool"}})");
  file.close();

  ConfigRepository repository(path);

  EXPECT_FALSE(repository.loadUtilityConfig().chat_title_generation_enabled.has_value());
}

TEST(ConfigRepository, ChatTitleModelOverrideRoundTripsExactly) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig original;
  original.chat_title_model_override =
      holonight_domain::ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("haiku")};

  ASSERT_TRUE(repository.saveUtilityConfig(original).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  EXPECT_EQ(loaded, original);
}

TEST(ConfigRepository, LoadMissingChatTitleModelOverrideReturnsNullopt) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));

  EXPECT_FALSE(repository.loadUtilityConfig().chat_title_model_override.has_value());
}

TEST(ConfigRepository, SaveThenLoadUnsetChatTitleModelOverrideRoundTripsToDefault) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig set;
  set.chat_title_model_override =
      holonight_domain::ModelId{.provider_id = QStringLiteral("google"), .model_name = QStringLiteral("gemini")};
  ASSERT_TRUE(repository.saveUtilityConfig(set).has_value());

  ASSERT_TRUE(repository.saveUtilityConfig(UtilityConfig{}).has_value());
  const UtilityConfig loaded = repository.loadUtilityConfig();

  EXPECT_FALSE(loaded.chat_title_model_override.has_value());
}

TEST(ConfigRepository, AllThreeUtilityFieldsRoundTripIndependently) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  UtilityConfig original;
  original.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("ollama"), .model_name = QStringLiteral("llama3")};
  original.chat_title_generation_enabled = false;
  original.chat_title_model_override =
      holonight_domain::ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-5")};

  ASSERT_TRUE(repository.saveUtilityConfig(original).has_value());

  EXPECT_EQ(repository.loadUtilityConfig(), original);
}

TEST(ConfigRepository, MissingNewSchemaAndLegacyProvidersLoadsEmptyState) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));

  EXPECT_EQ(repository.loadProviderState(), ProviderState{});
}

TEST(ConfigRepository, LegacyMigrationIncludesOnlyPresentValidSectionsInStableOrder) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"json({
    "providers": {
      "google": {"default_model": "gemini"},
      "openai": {"temperature": 0.4},
      "ollama": "invalid"
    }
  })json");
  file.close();

  ConfigRepository repository(path);
  const ProviderState state = repository.loadProviderState();

  ASSERT_EQ(state.instances.size(), 2U);
  EXPECT_EQ(state.instances[0].id, QStringLiteral("openai"));
  EXPECT_EQ(state.instances[0].type, ProviderType::OpenAi);
  EXPECT_EQ(state.instances[0].display_name, QStringLiteral("OpenAI"));
  EXPECT_EQ(std::get<OpenAIProviderConfig>(state.instances[0].settings).temperature, 0.4);
  EXPECT_EQ(state.instances[1].id, QStringLiteral("google"));
  EXPECT_EQ(std::get<GoogleProviderConfig>(state.instances[1].settings).default_model, QStringLiteral("gemini"));
}

TEST(ConfigRepository, FullLegacyMigrationIncludesAllProvidersWithTypedSettings) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"json({"providers":{
    "google":{"max_output_tokens":4096},
    "anthropic":{"temperature":0.2},
    "openai":{"default_model":"gpt"},
    "ollama":{"context_window":8192}
  }})json");
  file.close();

  const ProviderState state = ConfigRepository(path).loadProviderState();

  ASSERT_EQ(state.instances.size(), 4U);
  EXPECT_EQ(state.instances[0].type, ProviderType::Ollama);
  EXPECT_EQ(std::get<OllamaProviderConfig>(state.instances[0].settings).context_window, 8192);
  EXPECT_EQ(state.instances[1].type, ProviderType::OpenAi);
  EXPECT_EQ(std::get<OpenAIProviderConfig>(state.instances[1].settings).default_model, QStringLiteral("gpt"));
  EXPECT_EQ(state.instances[2].type, ProviderType::Anthropic);
  EXPECT_DOUBLE_EQ(std::get<AnthropicProviderConfig>(state.instances[2].settings).temperature, 0.2);
  EXPECT_EQ(state.instances[3].type, ProviderType::Google);
  EXPECT_EQ(std::get<GoogleProviderConfig>(state.instances[3].settings).max_output_tokens, 4096);
}

TEST(ConfigRepository, PersistedMigrationIsIdempotentWhenLegacyDataLaterChanges) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"providers":{"ollama":{"default_model":"original"}}})");
  file.close();
  ConfigRepository repository(path);
  const ProviderState migrated = repository.loadProviderState();
  ASSERT_TRUE(repository.saveProviderState(migrated).has_value());

  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  file.close();
  QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
  providers[QStringLiteral("openai")] = QJsonObject{{QStringLiteral("default_model"), QStringLiteral("late")}};
  root[QStringLiteral("providers")] = providers;
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(root).toJson());
  file.close();

  EXPECT_EQ(repository.loadProviderState(), migrated);
}

TEST(ConfigRepository, NewSchemaPreventsLegacyDataFromBeingMerged) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"json({
    "provider_instances": {"schema_version": 1, "instances": [], "tombstones": []},
    "providers": {"ollama": {"default_model": "legacy"}}
  })json");
  file.close();

  ConfigRepository repository(path);

  EXPECT_EQ(repository.loadProviderState(), ProviderState{});
}

TEST(ConfigRepository, ProviderStateRoundTripsAllSettingsTypesAndTombstonesInOrder) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  ProviderState expected;
  expected.instances = {
      {.id = QStringLiteral("11111111-1111-4111-8111-111111111111"),
       .type = ProviderType::Ollama,
       .display_name = QStringLiteral("Local"),
       .enabled = true,
       .settings = OllamaProviderConfig{.base_url = QStringLiteral("http://host:11434"),
                                        .default_model = QStringLiteral("qwen"),
                                        .context_window = 8192,
                                        .temperature = 0.2}},
      {.id = QStringLiteral("22222222-2222-4222-8222-222222222222"),
       .type = ProviderType::OpenAi,
       .display_name = QStringLiteral("Work"),
       .enabled = false,
       .settings = OpenAIProviderConfig{.default_model = QStringLiteral("gpt-5"), .temperature = 0.5}},
      {.id = QStringLiteral("33333333-3333-4333-8333-333333333333"),
       .type = ProviderType::Anthropic,
       .display_name = QStringLiteral("Claude"),
       .enabled = true,
       .settings = AnthropicProviderConfig{.max_output_tokens = 2048}},
      {.id = QStringLiteral("44444444-4444-4444-8444-444444444444"),
       .type = ProviderType::Google,
       .display_name = QStringLiteral("Gemini"),
       .enabled = true,
       .settings = GoogleProviderConfig{.max_output_tokens = 4096}},
  };
  expected.tombstones = {{.instance_id = QStringLiteral("openai"),
                          .type = ProviderType::OpenAi,
                          .last_display_name = QStringLiteral("Old OpenAI")}};

  ASSERT_TRUE(repository.saveProviderState(expected).has_value());

  EXPECT_EQ(repository.loadProviderState(), expected);
}

TEST(ConfigRepository, AnthropicToolCallingEnabledRoundTripsTrue) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  const ProviderState state{.instances = {{.id = QStringLiteral("anthropic"),
                                           .type = ProviderType::Anthropic,
                                           .display_name = QStringLiteral("Claude"),
                                           .enabled = true,
                                           .settings = AnthropicProviderConfig{.tool_calling_enabled = true}}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_TRUE(std::get<AnthropicProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, AnthropicToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  // Simulates a config.json written before this cycle shipped -- the settings object has no
  // "tool_calling_enabled" key at all (REQ-F-010(2)).
  file.write(R"json({
    "provider_instances": {
      "schema_version": 1,
      "instances": [
        {"id": "anthropic", "type": "anthropic", "name": "Claude", "enabled": true,
         "settings": {"base_url": "https://api.anthropic.com", "default_model": "", "temperature": 1.0,
                      "max_output_tokens": 1024}}
      ],
      "tombstones": []
    }
  })json");
  file.close();

  ConfigRepository repository(path);

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_FALSE(std::get<AnthropicProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, GoogleToolCallingEnabledRoundTripsTrue) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  const ProviderState state{.instances = {{.id = QStringLiteral("google"),
                                           .type = ProviderType::Google,
                                           .display_name = QStringLiteral("Gemini"),
                                           .enabled = true,
                                           .settings = GoogleProviderConfig{.tool_calling_enabled = true}}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_TRUE(std::get<GoogleProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, GoogleToolCallingEnabledRoundTripsFalse) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  const ProviderState state{.instances = {{.id = QStringLiteral("google"),
                                           .type = ProviderType::Google,
                                           .display_name = QStringLiteral("Gemini"),
                                           .enabled = true,
                                           .settings = GoogleProviderConfig{.tool_calling_enabled = false}}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_FALSE(std::get<GoogleProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, GoogleToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  // Simulates a config.json written before this cycle shipped -- the settings object has no
  // "tool_calling_enabled" key at all (REQ-F-014).
  file.write(R"json({
    "provider_instances": {
      "schema_version": 1,
      "instances": [
        {"id": "google", "type": "google", "name": "Gemini", "enabled": true,
         "settings": {"base_url": "https://generativelanguage.googleapis.com", "default_model": "",
                      "temperature": 1.0, "max_output_tokens": 8192}}
      ],
      "tombstones": []
    }
  })json");
  file.close();

  ConfigRepository repository(path);

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_FALSE(std::get<GoogleProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, OllamaToolCallingEnabledRoundTripsTrue) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  const ProviderState state{.instances = {{.id = QStringLiteral("ollama"),
                                           .type = ProviderType::Ollama,
                                           .display_name = QStringLiteral("Ollama"),
                                           .enabled = true,
                                           .settings = OllamaProviderConfig{.tool_calling_enabled = true}}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_TRUE(std::get<OllamaProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, OllamaToolCallingEnabledRoundTripsFalse) {
  QTemporaryDir dir;
  ConfigRepository repository(scratchFilePath(dir));
  const ProviderState state{.instances = {{.id = QStringLiteral("ollama"),
                                           .type = ProviderType::Ollama,
                                           .display_name = QStringLiteral("Ollama"),
                                           .enabled = true,
                                           .settings = OllamaProviderConfig{.tool_calling_enabled = false}}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_FALSE(std::get<OllamaProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, OllamaToolCallingEnabledDefaultsToFalseWhenKeyMissingFromOnDiskConfig) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  // Simulates a config.json written before this cycle shipped -- the settings object has no
  // "tool_calling_enabled" key at all (REQ-F-012).
  file.write(R"json({
    "provider_instances": {
      "schema_version": 1,
      "instances": [
        {"id": "ollama", "type": "ollama", "name": "Ollama", "enabled": true,
         "settings": {"base_url": "http://localhost:11434", "default_model": "",
                      "temperature": 0.7, "context_window": 4096}}
      ],
      "tombstones": []
    }
  })json");
  file.close();

  ConfigRepository repository(path);

  const ProviderState loaded = repository.loadProviderState();
  ASSERT_EQ(loaded.instances.size(), 1);
  EXPECT_FALSE(std::get<OllamaProviderConfig>(loaded.instances[0].settings).tool_calling_enabled);
}

TEST(ConfigRepository, TombstonesSerializeOnlyHistoricalIdentity) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  ConfigRepository repository(path);
  const ProviderState state{.tombstones = {{.instance_id = QStringLiteral("550e8400-e29b-41d4-a716-446655440000"),
                                            .type = ProviderType::OpenAi,
                                            .last_display_name = QStringLiteral("Former work account")}}};

  ASSERT_TRUE(repository.saveProviderState(state).has_value());

  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QJsonArray tombstones = QJsonDocument::fromJson(file.readAll())
                                    .object()
                                    .value(QStringLiteral("provider_instances"))
                                    .toObject()
                                    .value(QStringLiteral("tombstones"))
                                    .toArray();
  ASSERT_EQ(tombstones.size(), 1);
  const QJsonObject tombstone = tombstones.at(0).toObject();
  EXPECT_EQ(tombstone.keys(), (QStringList{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type")}));
  EXPECT_EQ(tombstone.value(QStringLiteral("id")).toString(), QStringLiteral("550e8400-e29b-41d4-a716-446655440000"));
  EXPECT_EQ(tombstone.value(QStringLiteral("type")).toString(), QStringLiteral("openai"));
  EXPECT_EQ(tombstone.value(QStringLiteral("name")).toString(), QStringLiteral("Former work account"));
}

TEST(ConfigRepository, SaveProviderStatePreservesUnrelatedRootAndLegacyCachedData) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"unrelated":{"keep":42},"providers":{"ollama":{"cached_models":["qwen"]}}})");
  file.close();
  ConfigRepository repository(path);

  ASSERT_TRUE(repository.saveProviderState(ProviderState{}).has_value());

  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  EXPECT_EQ(root.value(QStringLiteral("unrelated")).toObject().value(QStringLiteral("keep")).toInt(), 42);
  EXPECT_EQ(root.value(QStringLiteral("providers"))
                .toObject()
                .value(QStringLiteral("ollama"))
                .toObject()
                .value(QStringLiteral("cached_models"))
                .toArray()
                .first()
                .toString(),
            QStringLiteral("qwen"));
}

TEST(ConfigRepository, MalformedNewSchemaEntriesAreSkippedWithoutChangingValidOrder) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"json({"provider_instances":{"schema_version":1,"instances":[
    {"id":"11111111-1111-4111-8111-111111111111","type":"ollama","name":"One","enabled":true,"settings":{}},
    {"id":"bad","type":"google","name":"Bad","enabled":true,"settings":{}},
    {"id":"22222222-2222-4222-8222-222222222222","type":"google","name":"Two","enabled":false,"settings":{}}
  ],"tombstones":[]}})json");
  file.close();
  ConfigRepository repository(path);

  const ProviderState state = repository.loadProviderState();

  ASSERT_EQ(state.instances.size(), 2U);
  EXPECT_EQ(state.instances[0].display_name, QStringLiteral("One"));
  EXPECT_EQ(state.instances[1].display_name, QStringLiteral("Two"));
}

TEST(ConfigRepository, DuplicateIdsAndCaseFoldedNamesKeepFirstValidEntry) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"json({"provider_instances":{"schema_version":1,"instances":[
    {"id":"11111111-1111-4111-8111-111111111111","type":"ollama","name":"Alpha","enabled":true,"settings":{}},
    {"id":"11111111-1111-4111-8111-111111111111","type":"openai","name":"Beta","enabled":true,"settings":{}},
    {"id":"22222222-2222-4222-8222-222222222222","type":"google","name":"alpha","enabled":true,"settings":{}}
  ],"tombstones":[]}})json");
  file.close();
  ConfigRepository repository(path);

  const ProviderState state = repository.loadProviderState();

  ASSERT_EQ(state.instances.size(), 1U);
  EXPECT_EQ(state.instances.front().display_name, QStringLiteral("Alpha"));
}

TEST(ConfigRepository, UnsupportedSchemaVersionDoesNotFallBackToLegacyMigration) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"provider_instances":{"schema_version":99},"providers":{"ollama":{}}})");
  file.close();
  ConfigRepository repository(path);

  EXPECT_EQ(repository.loadProviderState(), ProviderState{});
}

TEST(ConfigRepository, SaveProviderStateRejectsMismatchedTypeWithoutTouchingExistingFile) {
  QTemporaryDir dir;
  const QString path = scratchFilePath(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write(R"({"sentinel":true})");
  file.close();
  ConfigRepository repository(path);
  ProviderState invalid;
  invalid.instances.push_back({.id = QStringLiteral("ollama"),
                               .type = ProviderType::Ollama,
                               .display_name = QStringLiteral("Ollama"),
                               .enabled = true,
                               .settings = GoogleProviderConfig{}});

  EXPECT_FALSE(repository.saveProviderState(invalid).has_value());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_TRUE(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("sentinel")).toBool());
}

TEST(ConfigRepository, SaveProviderStateAtomicFailurePreservesExistingFile) {
#ifndef _WIN32
  if (geteuid() == 0) {
    GTEST_SKIP() << "root bypasses directory permission checks";
  }
#endif
  QTemporaryDir dir;
  const QString lockedDir = dir.filePath(QStringLiteral("locked"));
  ASSERT_TRUE(QDir().mkpath(lockedDir));
  const QString path = lockedDir + QStringLiteral("/config.json");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  const QByteArray original = R"({"sentinel":"unchanged"})";
  ASSERT_EQ(file.write(original), original.size());
  file.close();
  ASSERT_TRUE(QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

  const auto result = ConfigRepository(path).saveProviderState(ProviderState{});

  EXPECT_FALSE(result.has_value());
  ASSERT_TRUE(
      QFile::setPermissions(lockedDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), original);
}

}  // namespace
}  // namespace holonight_config
