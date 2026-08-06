#pragma once

#include <QObject>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <holonight_config/provider_config.h>
#include <optional>

namespace holonight_application {

class ProviderDraftSession : public QObject {
  Q_OBJECT

 public:
  enum class CredentialEdit : std::uint8_t { Keep, Store, Remove };
  Q_ENUM(CredentialEdit)

  using NameValidator = std::function<bool(const QString&, const QString&)>;

  explicit ProviderDraftSession(std::optional<holonight_config::ProviderInstanceConfig> original,
                                holonight_config::ProviderInstanceConfig editable, NameValidator name_validator = {},
                                QObject* parent = nullptr);

  [[nodiscard]] const std::optional<holonight_config::ProviderInstanceConfig>& original() const;
  [[nodiscard]] const holonight_config::ProviderInstanceConfig& editable() const;
  [[nodiscard]] bool isAddition() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] CredentialEdit credentialEdit() const;
  [[nodiscard]] QString credentialValue() const;
  [[nodiscard]] QStringList validationErrors() const;

  void setDisplayName(QString display_name);
  void setEnabled(bool enabled);
  [[nodiscard]] bool setSettings(holonight_config::ProviderSettings settings);
  void setCredential(QString value);
  void removeCredential();
  void keepCredential();
  void resetToDefaults();
  void discard();
  [[nodiscard]] bool validate();
  void commitConfiguration();
  void commitCredential();

 Q_SIGNALS:
  void changed();
  void dirtyChanged();
  void validationErrorsChanged();

 private:
  [[nodiscard]] static holonight_config::ProviderSettings defaultSettings(holonight_config::ProviderType type);
  void mutate(const std::function<void()>& operation);
  void clearValidationErrors();

  std::optional<holonight_config::ProviderInstanceConfig> original_;
  holonight_config::ProviderInstanceConfig initial_;
  holonight_config::ProviderInstanceConfig editable_;
  NameValidator name_validator_;
  CredentialEdit credential_edit_ = CredentialEdit::Keep;
  QString credential_value_;
  QStringList validation_errors_;
};

}  // namespace holonight_application
