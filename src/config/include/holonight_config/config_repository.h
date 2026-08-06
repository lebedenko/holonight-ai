#pragma once

#include "holonight_config/provider_config.h"
#include "holonight_config/utility_config.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <expected>

namespace holonight_config {

// Synchronous, GUI-thread JSON I/O — see DESIGN.md §5.2 for why this deliberately does NOT follow
// the worker-thread precedent SqliteConversationRepository/SecretServiceCredentialStore use.
// Constructed with an explicit file path so GTest can inject a temp-dir path (REQ-NF-003) instead
// of the real resolveConfigFilePath().
class ConfigRepository {
 public:
  explicit ConfigRepository(QString file_path);

  // Loads the ordered instance schema. If it is absent, returns an in-memory projection of the
  // physically present legacy provider sections; migration becomes durable on saveProviderState().
  [[nodiscard]] ProviderState loadProviderState() const;

  // Atomically replaces only the provider_instances root member, preserving all adjacent data.
  [[nodiscard]] std::expected<void, QString> saveProviderState(const ProviderState& state) const;

  // Atomically replaces provider state and utility configuration in one root write. Used when
  // deleting a provider that owns the utility default so neither half can become durable alone.
  [[nodiscard]] std::expected<void, QString> saveProviderStateAndUtilityConfig(
      const ProviderState& state, const UtilityConfig& utility_config) const;

  [[nodiscard]] QStringList loadCachedModels(const QString& provider_id) const;
  [[nodiscard]] std::expected<void, QString> saveCachedModels(const QString& provider_id,
                                                              const QStringList& model_names) const;

  // REQ-F-001: never throws. Absent "utility" key, absent "default_utility_model", or a
  // null/malformed value all load as std::nullopt.
  [[nodiscard]] UtilityConfig loadUtilityConfig() const;

  // REQ-F-001: same std::expected<void, QString> convention as the provider save methods.
  [[nodiscard]] std::expected<void, QString> saveUtilityConfig(const UtilityConfig& config) const;

 private:
  // Reads the whole config file and returns its top-level JSON object, or an empty object on a
  // missing file, unreadable file, or malformed JSON.
  [[nodiscard]] QJsonObject readRootObject() const;

  // Writes `root` to the config file (creating the parent directory if needed), indented.
  // Shared by the state, cached-model, and utility writers so each preserves adjacent root data.
  [[nodiscard]] std::expected<void, QString> writeRootObject(const QJsonObject& root) const;

  QString file_path_;
};

}  // namespace holonight_config
