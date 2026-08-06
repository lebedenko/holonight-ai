#include "holonight_application/provider_draft_session.h"

#include <utility>

namespace holonight_application {

using namespace holonight_config;

ProviderDraftSession::ProviderDraftSession(std::optional<ProviderInstanceConfig> original,
                                           ProviderInstanceConfig editable, NameValidator name_validator,
                                           QObject* parent)
    : QObject(parent),
      original_(std::move(original)),
      initial_(editable),
      editable_(std::move(editable)),
      name_validator_(std::move(name_validator)) {}

const std::optional<ProviderInstanceConfig>& ProviderDraftSession::original() const { return original_; }

const ProviderInstanceConfig& ProviderDraftSession::editable() const { return editable_; }

bool ProviderDraftSession::isAddition() const { return !original_.has_value(); }

bool ProviderDraftSession::dirty() const {
  const auto& baseline = original_.has_value() ? *original_ : initial_;
  return editable_ != baseline || credential_edit_ != CredentialEdit::Keep;
}

ProviderDraftSession::CredentialEdit ProviderDraftSession::credentialEdit() const { return credential_edit_; }

QString ProviderDraftSession::credentialValue() const { return credential_value_; }

QStringList ProviderDraftSession::validationErrors() const { return validation_errors_; }

void ProviderDraftSession::setDisplayName(QString display_name) {
  mutate([this, display_name = std::move(display_name)] { editable_.display_name = display_name; });
}

void ProviderDraftSession::setEnabled(bool enabled) {
  mutate([this, enabled] { editable_.enabled = enabled; });
}

bool ProviderDraftSession::setSettings(ProviderSettings settings) {
  const bool matching_type =
      (editable_.type == ProviderType::Ollama && std::holds_alternative<OllamaProviderConfig>(settings)) ||
      (editable_.type == ProviderType::OpenAi && std::holds_alternative<OpenAIProviderConfig>(settings)) ||
      (editable_.type == ProviderType::Anthropic && std::holds_alternative<AnthropicProviderConfig>(settings)) ||
      (editable_.type == ProviderType::Google && std::holds_alternative<GoogleProviderConfig>(settings));
  if (!matching_type) {
    return false;
  }
  mutate([this, settings = std::move(settings)]() mutable { editable_.settings = std::move(settings); });
  return true;
}

void ProviderDraftSession::setCredential(QString value) {
  mutate([this, value = std::move(value)] {
    credential_edit_ = CredentialEdit::Store;
    credential_value_ = value;
  });
}

void ProviderDraftSession::removeCredential() {
  mutate([this] {
    credential_edit_ = CredentialEdit::Remove;
    credential_value_.clear();
  });
}

void ProviderDraftSession::keepCredential() {
  mutate([this] {
    credential_edit_ = CredentialEdit::Keep;
    credential_value_.clear();
  });
}

void ProviderDraftSession::resetToDefaults() {
  mutate([this] {
    editable_.settings = defaultSettings(editable_.type);
    credential_edit_ = CredentialEdit::Remove;
    credential_value_.clear();
  });
}

void ProviderDraftSession::discard() {
  mutate([this] {
    editable_ = original_.value_or(initial_);
    credential_edit_ = CredentialEdit::Keep;
    credential_value_.clear();
  });
}

bool ProviderDraftSession::validate() {
  QStringList errors;
  editable_.display_name = editable_.display_name.trimmed();
  if (editable_.display_name.isEmpty()) {
    errors.push_back(tr("Name is required."));
  } else if (name_validator_ && !name_validator_(editable_.display_name, editable_.id)) {
    errors.push_back(tr("Name must be unique."));
  }

  std::visit(
      [&errors, this](const auto& settings) {
        if (settings.base_url.trimmed().isEmpty()) {
          errors.push_back(tr("Base URL is required."));
        }
        if (settings.temperature < 0.0 || settings.temperature > 2.0) {
          errors.push_back(tr("Temperature must be between 0 and 2."));
        }
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, OllamaProviderConfig>) {
          if (settings.context_window < 128 || settings.context_window > 1'000'000) {
            errors.push_back(tr("Context window must be between 128 and 1,000,000."));
          }
        } else if constexpr (std::is_same_v<Settings, AnthropicProviderConfig>) {
          if (settings.temperature > 1.0) {
            errors.push_back(tr("Anthropic temperature must be between 0 and 1."));
          }
          if (settings.max_output_tokens <= 0) {
            errors.push_back(tr("Maximum output tokens must be positive."));
          }
        } else if constexpr (std::is_same_v<Settings, GoogleProviderConfig>) {
          if (settings.max_output_tokens <= 0) {
            errors.push_back(tr("Maximum output tokens must be positive."));
          }
        }
      },
      editable_.settings);
  if (credential_edit_ == CredentialEdit::Store && credential_value_.isEmpty()) {
    errors.push_back(tr("Credential cannot be empty."));
  }
  if (validation_errors_ != errors) {
    validation_errors_ = std::move(errors);
    emit validationErrorsChanged();
  }
  return validation_errors_.isEmpty();
}

void ProviderDraftSession::commitConfiguration() {
  const bool was_dirty = dirty();
  original_ = editable_;
  initial_ = editable_;
  clearValidationErrors();
  emit changed();
  if (was_dirty != dirty()) {
    emit dirtyChanged();
  }
}

void ProviderDraftSession::commitCredential() {
  const bool was_dirty = dirty();
  credential_edit_ = CredentialEdit::Keep;
  credential_value_.clear();
  emit changed();
  if (was_dirty != dirty()) {
    emit dirtyChanged();
  }
}

ProviderSettings ProviderDraftSession::defaultSettings(ProviderType type) {
  switch (type) {
    case ProviderType::Ollama:
      return OllamaProviderConfig{};
    case ProviderType::OpenAi:
      return OpenAIProviderConfig{};
    case ProviderType::Anthropic:
      return AnthropicProviderConfig{};
    case ProviderType::Google:
      return GoogleProviderConfig{};
  }
  return OllamaProviderConfig{};
}

void ProviderDraftSession::mutate(const std::function<void()>& operation) {
  const bool was_dirty = dirty();
  operation();
  clearValidationErrors();
  emit changed();
  if (was_dirty != dirty()) {
    emit dirtyChanged();
  }
}

void ProviderDraftSession::clearValidationErrors() {
  if (validation_errors_.isEmpty()) {
    return;
  }
  validation_errors_.clear();
  emit validationErrorsChanged();
}

}  // namespace holonight_application
