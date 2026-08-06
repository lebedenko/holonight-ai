#include "holonight_config/config_repository.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <array>
#include <cmath>
#include <optional>
#include <type_traits>
#include <utility>

namespace holonight_config {

namespace {

constexpr auto kProvidersKey = "providers";
constexpr auto kProviderInstancesKey = "provider_instances";
constexpr auto kSchemaVersionKey = "schema_version";
constexpr auto kInstancesKey = "instances";
constexpr auto kTombstonesKey = "tombstones";
constexpr auto kIdKey = "id";
constexpr auto kTypeKey = "type";
constexpr auto kNameKey = "name";
constexpr auto kEnabledKey = "enabled";
constexpr auto kSettingsKey = "settings";
constexpr auto kOllamaKey = "ollama";
constexpr auto kOpenAiKey = "openai";
constexpr auto kAnthropicKey = "anthropic";
constexpr auto kGoogleKey = "google";
constexpr auto kBaseUrlKey = "base_url";
constexpr auto kDefaultModelKey = "default_model";
constexpr auto kContextWindowKey = "context_window";
constexpr auto kTemperatureKey = "temperature";
constexpr auto kMaxOutputTokensKey = "max_output_tokens";
constexpr auto kToolCallingEnabledKey = "tool_calling_enabled";
constexpr auto kCachedModelsKey = "cached_models";
constexpr auto kUtilityKey = "utility";
constexpr auto kDefaultUtilityModelKey = "default_utility_model";
constexpr auto kChatTitleGenerationEnabledKey = "chat_title_generation_enabled";
constexpr auto kChatTitleModelOverrideKey = "chat_title_model_override";
constexpr auto kProviderIdKey = "provider_id";
constexpr auto kModelNameKey = "model_name";
constexpr int kProviderSchemaVersion = 1;

bool isValidInstanceId(const QString& id) {
  if (id == QLatin1String(kOllamaKey) || id == QLatin1String(kOpenAiKey) || id == QLatin1String(kAnthropicKey) ||
      id == QLatin1String(kGoogleKey)) {
    return true;
  }
  return !id.isEmpty() && !QUuid::fromString(id).isNull();
}

bool hasType(const QJsonObject& object, const char* key, QJsonValue::Type type) {
  const auto value = object.value(QLatin1String(key));
  return value.isUndefined() || value.type() == type;
}

bool hasFiniteNumber(const QJsonObject& object, const char* key) {
  const auto value = object.value(QLatin1String(key));
  return value.isUndefined() || (value.isDouble() && std::isfinite(value.toDouble()));
}

std::optional<ProviderSettings> parseSettings(ProviderType type, const QJsonObject& object) {
  if (!hasType(object, kBaseUrlKey, QJsonValue::String) || !hasType(object, kDefaultModelKey, QJsonValue::String) ||
      !hasFiniteNumber(object, kTemperatureKey)) {
    return std::nullopt;
  }

  switch (type) {
    case ProviderType::Ollama: {
      if (!hasFiniteNumber(object, kContextWindowKey)) {
        return std::nullopt;
      }
      OllamaProviderConfig config;
      config.base_url = object.value(QLatin1String(kBaseUrlKey)).toString(config.base_url);
      config.default_model = object.value(QLatin1String(kDefaultModelKey)).toString(config.default_model);
      config.context_window = object.value(QLatin1String(kContextWindowKey)).toInt(config.context_window);
      config.temperature = object.value(QLatin1String(kTemperatureKey)).toDouble(config.temperature);
      config.tool_calling_enabled = object.value(QLatin1String(kToolCallingEnabledKey)).toBool(false);
      if (config.base_url.trimmed().isEmpty() || config.context_window <= 0 || config.temperature < 0.0 ||
          config.temperature > 2.0) {
        return std::nullopt;
      }
      return config;
    }
    case ProviderType::OpenAi: {
      OpenAIProviderConfig config;
      config.base_url = object.value(QLatin1String(kBaseUrlKey)).toString(config.base_url);
      config.default_model = object.value(QLatin1String(kDefaultModelKey)).toString(config.default_model);
      config.temperature = object.value(QLatin1String(kTemperatureKey)).toDouble(config.temperature);
      config.tool_calling_enabled = object.value(QLatin1String(kToolCallingEnabledKey)).toBool(false);
      if (config.base_url.trimmed().isEmpty() || config.temperature < 0.0 || config.temperature > 2.0) {
        return std::nullopt;
      }
      return config;
    }
    case ProviderType::Anthropic:
    case ProviderType::Google: {
      if (!hasFiniteNumber(object, kMaxOutputTokensKey)) {
        return std::nullopt;
      }
      const int maxOutputTokens =
          object.value(QLatin1String(kMaxOutputTokensKey))
              .toInt(type == ProviderType::Anthropic ? AnthropicProviderConfig{}.max_output_tokens
                                                     : GoogleProviderConfig{}.max_output_tokens);
      const double temperature = object.value(QLatin1String(kTemperatureKey)).toDouble(1.0);
      const QString baseUrl = object.value(QLatin1String(kBaseUrlKey))
                                  .toString(type == ProviderType::Anthropic ? AnthropicProviderConfig{}.base_url
                                                                            : GoogleProviderConfig{}.base_url);
      const QString defaultModel = object.value(QLatin1String(kDefaultModelKey)).toString();
      if (baseUrl.trimmed().isEmpty() || maxOutputTokens <= 0 || temperature < 0.0 || temperature > 2.0) {
        return std::nullopt;
      }
      // REQ-F-010(2)/REQ-F-014: a missing key (pre-existing on-disk config written before this
      // cycle shipped, or a present-but-non-boolean value) must decode as false, never fail
      // parsing. Applies identically to both Anthropic and Google.
      const bool toolCallingEnabled = object.value(QLatin1String(kToolCallingEnabledKey)).toBool(false);
      if (type == ProviderType::Anthropic) {
        return AnthropicProviderConfig{.base_url = baseUrl,
                                       .default_model = defaultModel,
                                       .temperature = temperature,
                                       .max_output_tokens = maxOutputTokens,
                                       .tool_calling_enabled = toolCallingEnabled};
      }
      return GoogleProviderConfig{.base_url = baseUrl,
                                  .default_model = defaultModel,
                                  .temperature = temperature,
                                  .max_output_tokens = maxOutputTokens,
                                  .tool_calling_enabled = toolCallingEnabled};
    }
  }
  return std::nullopt;
}

QJsonObject serializeSettings(const ProviderSettings& settings) {
  return std::visit(
      [](const auto& config) {
        QJsonObject object{{QLatin1String(kBaseUrlKey), config.base_url},
                           {QLatin1String(kDefaultModelKey), config.default_model},
                           {QLatin1String(kTemperatureKey), config.temperature}};
        using Config = std::decay_t<decltype(config)>;
        if constexpr (std::is_same_v<Config, OllamaProviderConfig>) {
          object[QLatin1String(kContextWindowKey)] = config.context_window;
        } else if constexpr (std::is_same_v<Config, AnthropicProviderConfig> ||
                             std::is_same_v<Config, GoogleProviderConfig>) {
          object[QLatin1String(kMaxOutputTokensKey)] = config.max_output_tokens;
        }
        if constexpr (std::is_same_v<Config, OllamaProviderConfig> || std::is_same_v<Config, OpenAIProviderConfig> ||
                      std::is_same_v<Config, AnthropicProviderConfig> || std::is_same_v<Config, GoogleProviderConfig>) {
          object[QLatin1String(kToolCallingEnabledKey)] = config.tool_calling_enabled;
        }
        return object;
      },
      settings);
}

bool settingsMatchType(ProviderType type, const ProviderSettings& settings) {
  return settings.index() == static_cast<std::size_t>(type);
}

void warnSkipped(const QString& what) {
  qWarning().noquote() << QStringLiteral("holonight_config: skipped invalid %1").arg(what);
}

}  // namespace

ConfigRepository::ConfigRepository(QString file_path) : file_path_(std::move(file_path)) {}

QJsonObject ConfigRepository::readRootObject() const {
  QFile file(file_path_);
  if (!file.exists()) {
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning().noquote()
        << QStringLiteral("holonight_config: failed to open %1: %2").arg(file_path_, file.errorString());
    return {};
  }

  const QByteArray content = file.readAll();
  if (content.trimmed().isEmpty()) {
    return {};
  }

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(content, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning().noquote()
        << QStringLiteral("holonight_config: malformed JSON in %1: %2").arg(file_path_, parseError.errorString());
    return {};
  }

  return doc.object();
}

std::expected<void, QString> ConfigRepository::writeRootObject(const QJsonObject& root) const {
  const QFileInfo fileInfo(file_path_);
  if (!fileInfo.dir().exists() && !QDir().mkpath(fileInfo.absolutePath())) {
    return std::unexpected(QStringLiteral("Failed to create directory %1").arg(fileInfo.absolutePath()));
  }

  QSaveFile file(file_path_);
  if (!file.open(QIODevice::WriteOnly)) {
    return std::unexpected(file.errorString());
  }

  const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (file.write(json) != json.size()) {
    return std::unexpected(file.errorString());
  }

  if (!file.commit()) {
    return std::unexpected(file.errorString());
  }

  return {};
}

ProviderState ConfigRepository::loadProviderState() const {
  const QJsonObject root = readRootObject();
  const QJsonValue stateValue = root.value(QLatin1String(kProviderInstancesKey));

  if (stateValue.isUndefined()) {
    ProviderState migrated;
    const QJsonObject legacyProviders = root.value(QLatin1String(kProvidersKey)).toObject();
    const std::array legacyTypes{ProviderType::Ollama, ProviderType::OpenAi, ProviderType::Anthropic,
                                 ProviderType::Google};
    for (const ProviderType type : legacyTypes) {
      const QString id = providerTypeToString(type);
      const QJsonValue legacyValue = legacyProviders.value(id);
      if (legacyValue.isUndefined()) {
        continue;
      }
      if (!legacyValue.isObject()) {
        warnSkipped(QStringLiteral("legacy provider '%1'").arg(id));
        continue;
      }
      const auto settings = parseSettings(type, legacyValue.toObject());
      if (!settings.has_value()) {
        warnSkipped(QStringLiteral("legacy provider '%1'").arg(id));
        continue;
      }
      migrated.instances.push_back(ProviderInstanceConfig{.id = id,
                                                          .type = type,
                                                          .display_name = defaultProviderDisplayName(type),
                                                          .enabled = true,
                                                          .settings = *settings});
    }
    return migrated;
  }

  if (!stateValue.isObject()) {
    warnSkipped(QStringLiteral("provider_instances state"));
    return {};
  }
  const QJsonObject stateObject = stateValue.toObject();
  if (stateObject.value(QLatin1String(kSchemaVersionKey)).toInt(-1) != kProviderSchemaVersion) {
    warnSkipped(QStringLiteral("provider_instances schema version"));
    return {};
  }

  ProviderState state;
  QSet<QString> ids;
  QSet<QString> names;
  const QJsonValue instancesValue = stateObject.value(QLatin1String(kInstancesKey));
  if (instancesValue.isArray()) {
    for (const auto& value : instancesValue.toArray()) {
      if (!value.isObject()) {
        warnSkipped(QStringLiteral("provider instance"));
        continue;
      }
      const QJsonObject object = value.toObject();
      const QString id = object.value(QLatin1String(kIdKey)).toString();
      const auto type = providerTypeFromString(object.value(QLatin1String(kTypeKey)).toString());
      const QString name = object.value(QLatin1String(kNameKey)).toString().trimmed();
      const QString foldedName = name.toCaseFolded();
      const QJsonValue enabledValue = object.value(QLatin1String(kEnabledKey));
      const QJsonValue settingsValue = object.value(QLatin1String(kSettingsKey));
      if (!isValidInstanceId(id) || !type.has_value() || name.isEmpty() || ids.contains(id) ||
          names.contains(foldedName) || !enabledValue.isBool() || !settingsValue.isObject()) {
        warnSkipped(QStringLiteral("provider instance '%1'").arg(id));
        continue;
      }
      const auto settings = parseSettings(*type, settingsValue.toObject());
      if (!settings.has_value()) {
        warnSkipped(QStringLiteral("provider instance '%1' settings").arg(id));
        continue;
      }
      ids.insert(id);
      names.insert(foldedName);
      state.instances.push_back(ProviderInstanceConfig{
          .id = id, .type = *type, .display_name = name, .enabled = enabledValue.toBool(), .settings = *settings});
    }
  }

  QSet<QString> tombstoneIds;
  const QJsonValue tombstonesValue = stateObject.value(QLatin1String(kTombstonesKey));
  if (tombstonesValue.isArray()) {
    for (const auto& value : tombstonesValue.toArray()) {
      if (!value.isObject()) {
        warnSkipped(QStringLiteral("provider tombstone"));
        continue;
      }
      const QJsonObject object = value.toObject();
      const QString id = object.value(QLatin1String(kIdKey)).toString();
      const auto type = providerTypeFromString(object.value(QLatin1String(kTypeKey)).toString());
      const QString name = object.value(QLatin1String(kNameKey)).toString().trimmed();
      if (!isValidInstanceId(id) || !type.has_value() || name.isEmpty() || tombstoneIds.contains(id)) {
        warnSkipped(QStringLiteral("provider tombstone '%1'").arg(id));
        continue;
      }
      tombstoneIds.insert(id);
      state.tombstones.push_back(ProviderTombstone{.instance_id = id, .type = *type, .last_display_name = name});
    }
  }
  return state;
}

namespace {

std::expected<QJsonObject, QString> serializeProviderState(const ProviderState& state) {
  QSet<QString> ids;
  QSet<QString> names;
  QJsonArray instances;
  for (const auto& instance : state.instances) {
    const QString name = instance.display_name.trimmed();
    const QString foldedName = name.toCaseFolded();
    if (!isValidInstanceId(instance.id) || name.isEmpty() || ids.contains(instance.id) || names.contains(foldedName) ||
        !settingsMatchType(instance.type, instance.settings)) {
      return std::unexpected(QStringLiteral("Invalid provider instance '%1'").arg(instance.id));
    }
    ids.insert(instance.id);
    names.insert(foldedName);
    instances.append(QJsonObject{{QLatin1String(kIdKey), instance.id},
                                 {QLatin1String(kTypeKey), providerTypeToString(instance.type)},
                                 {QLatin1String(kNameKey), name},
                                 {QLatin1String(kEnabledKey), instance.enabled},
                                 {QLatin1String(kSettingsKey), serializeSettings(instance.settings)}});
  }

  QSet<QString> tombstoneIds;
  QJsonArray tombstones;
  for (const auto& tombstone : state.tombstones) {
    const QString name = tombstone.last_display_name.trimmed();
    if (!isValidInstanceId(tombstone.instance_id) || name.isEmpty() || tombstoneIds.contains(tombstone.instance_id)) {
      return std::unexpected(QStringLiteral("Invalid provider tombstone '%1'").arg(tombstone.instance_id));
    }
    tombstoneIds.insert(tombstone.instance_id);
    tombstones.append(QJsonObject{{QLatin1String(kIdKey), tombstone.instance_id},
                                  {QLatin1String(kTypeKey), providerTypeToString(tombstone.type)},
                                  {QLatin1String(kNameKey), name}});
  }

  return QJsonObject{{QLatin1String(kSchemaVersionKey), kProviderSchemaVersion},
                     {QLatin1String(kInstancesKey), instances},
                     {QLatin1String(kTombstonesKey), tombstones}};
}

void applyUtilityConfig(QJsonObject& root, const UtilityConfig& config) {
  QJsonObject utility = root.value(QLatin1String(kUtilityKey)).toObject();
  if (config.default_utility_model.has_value()) {
    utility[QLatin1String(kDefaultUtilityModelKey)] =
        QJsonObject{{QLatin1String(kProviderIdKey), config.default_utility_model->provider_id},
                    {QLatin1String(kModelNameKey), config.default_utility_model->model_name}};
  } else {
    utility.remove(QLatin1String(kDefaultUtilityModelKey));
  }
  if (config.chat_title_generation_enabled.has_value()) {
    utility[QLatin1String(kChatTitleGenerationEnabledKey)] = *config.chat_title_generation_enabled;
  } else {
    utility.remove(QLatin1String(kChatTitleGenerationEnabledKey));
  }
  if (config.chat_title_model_override.has_value()) {
    utility[QLatin1String(kChatTitleModelOverrideKey)] =
        QJsonObject{{QLatin1String(kProviderIdKey), config.chat_title_model_override->provider_id},
                    {QLatin1String(kModelNameKey), config.chat_title_model_override->model_name}};
  } else {
    utility.remove(QLatin1String(kChatTitleModelOverrideKey));
  }
  root[QLatin1String(kUtilityKey)] = utility;
}

}  // namespace

std::expected<void, QString> ConfigRepository::saveProviderState(const ProviderState& state) const {
  const auto serialized = serializeProviderState(state);
  if (!serialized.has_value()) {
    return std::unexpected(serialized.error());
  }
  QJsonObject root = readRootObject();
  root[QLatin1String(kProviderInstancesKey)] = *serialized;
  return writeRootObject(root);
}

std::expected<void, QString> ConfigRepository::saveProviderStateAndUtilityConfig(
    const ProviderState& state, const UtilityConfig& utility_config) const {
  const auto serialized = serializeProviderState(state);
  if (!serialized.has_value()) {
    return std::unexpected(serialized.error());
  }
  QJsonObject root = readRootObject();
  root[QLatin1String(kProviderInstancesKey)] = *serialized;
  applyUtilityConfig(root, utility_config);
  return writeRootObject(root);
}

QStringList ConfigRepository::loadCachedModels(const QString& provider_id) const {
  const QJsonObject provider =
      readRootObject().value(QLatin1String(kProvidersKey)).toObject().value(provider_id).toObject();
  const QJsonValue cachedModels = provider.value(QLatin1String(kCachedModelsKey));
  if (!cachedModels.isArray()) {
    return {};
  }

  QStringList modelNames;
  for (const auto& value : cachedModels.toArray()) {
    if (value.isString() && !value.toString().isEmpty()) {
      modelNames.append(value.toString());
    }
  }
  return modelNames;
}

std::expected<void, QString> ConfigRepository::saveCachedModels(const QString& provider_id,
                                                                const QStringList& model_names) const {
  QJsonObject root = readRootObject();
  QJsonObject providers = root.value(QLatin1String(kProvidersKey)).toObject();
  QJsonObject provider = providers.value(provider_id).toObject();
  const QJsonArray cachedModels = QJsonArray::fromStringList(model_names);
  if (provider.value(QLatin1String(kCachedModelsKey)).toArray() == cachedModels) {
    return {};
  }
  provider[QLatin1String(kCachedModelsKey)] = cachedModels;
  providers[provider_id] = provider;
  root[QLatin1String(kProvidersKey)] = providers;
  return writeRootObject(root);
}

namespace {

std::optional<holonight_domain::ModelId> parseUtilityModelId(const QJsonObject& utility, const char* key) {
  const QJsonValue modelValue = utility.value(QLatin1String(key));
  if (!modelValue.isObject()) {
    return std::nullopt;
  }
  const QJsonObject model = modelValue.toObject();
  const QString providerId = model.value(QLatin1String(kProviderIdKey)).toString();
  const QString modelName = model.value(QLatin1String(kModelNameKey)).toString();
  if (providerId.isEmpty() || modelName.isEmpty()) {
    return std::nullopt;
  }
  return holonight_domain::ModelId{.provider_id = providerId, .model_name = modelName};
}

}  // namespace

UtilityConfig ConfigRepository::loadUtilityConfig() const {
  const QJsonValue utilityValue = readRootObject().value(QLatin1String(kUtilityKey));
  if (!utilityValue.isObject()) {
    return {};
  }
  const QJsonObject utility = utilityValue.toObject();

  UtilityConfig config;
  config.default_utility_model = parseUtilityModelId(utility, kDefaultUtilityModelKey);
  config.chat_title_model_override = parseUtilityModelId(utility, kChatTitleModelOverrideKey);

  const QJsonValue enabledValue = utility.value(QLatin1String(kChatTitleGenerationEnabledKey));
  if (enabledValue.isBool()) {
    config.chat_title_generation_enabled = enabledValue.toBool();
  }
  return config;
}

std::expected<void, QString> ConfigRepository::saveUtilityConfig(const UtilityConfig& config) const {
  QJsonObject root = readRootObject();
  applyUtilityConfig(root, config);
  return writeRootObject(root);
}

}  // namespace holonight_config
