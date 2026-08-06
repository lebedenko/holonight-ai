#include "holonight_application/utility_settings_controller.h"

#include "holonight_application/chat_view_model.h"
#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QQmlEngine>
#include <QVariantMap>

#include <algorithm>
#include <holonight_config/config_path.h>
#include <type_traits>
#include <utility>
#include <variant>

namespace holonight_application {

namespace {

constexpr double kUtilityTemperature = 0.3;
constexpr int kAnthropicUtilityMaxOutputTokens = 64;
constexpr int kGoogleUtilityMaxOutputTokens = 1024;

// Same isolation rule as UtilityTaskRunner's withUtilityGenerationParams() (REQ-F-016/REQ-NF-001) —
// duplicated rather than shared per DESIGN.md §9 decision 3 (this codebase's established "rule of
// three/four" convention for this exact router-construction pattern).
holonight_config::ProviderInstanceConfig withUtilityGenerationParams(
    holonight_config::ProviderInstanceConfig instance) {
  std::visit(
      [](auto& settings) {
        settings.temperature = kUtilityTemperature;
        using Settings = std::decay_t<decltype(settings)>;
        if constexpr (std::is_same_v<Settings, holonight_config::AnthropicProviderConfig>) {
          settings.max_output_tokens = kAnthropicUtilityMaxOutputTokens;
        } else if constexpr (std::is_same_v<Settings, holonight_config::GoogleProviderConfig>) {
          settings.max_output_tokens = kGoogleUtilityMaxOutputTokens;
        }
      },
      instance.settings);
  return instance;
}

CredentialPolicy credentialPolicy(holonight_config::ProviderType type) {
  return type == holonight_config::ProviderType::Ollama ? CredentialPolicy::Optional : CredentialPolicy::Required;
}

}  // namespace

UtilitySettingsController* UtilitySettingsController::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  return new UtilitySettingsController(qml_engine);
}

UtilitySettingsController::UtilitySettingsController(QQmlEngine* qml_engine, QObject* parent)
    : QObject(parent),
      qml_engine_(qml_engine),
      config_repository_(holonight_config::resolveConfigFilePath()),
      router_(std::make_unique<ProviderAdapterRouter>()),
      provider_state_(config_repository_.loadProviderState()),
      saved_(config_repository_.loadUtilityConfig()),
      draft_(saved_) {
  auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chat != nullptr);
  runtime_coordinator_ = std::make_unique<ProviderRuntimeCoordinator>(chat->credentialStoreForSettings());
  connect(runtime_coordinator_.get(), &ProviderRuntimeCoordinator::providerChanged, this,
          &UtilitySettingsController::onProviderChanged);

  auto http_client = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  for (const auto& instance : provider_state_.instances) {
    if (!instance.enabled || !router_->add(withUtilityGenerationParams(instance), http_client)) {
      continue;
    }
    runtime_coordinator_->registerProvider(
        instance.id, instance.display_name, credentialPolicy(instance.type),
        ProviderRuntimeOperations{
            .set_credential =
                [router = router_.get(), instance_id = instance.id](const QString& value) {
                  static_cast<void>(router->setCredential(instance_id, value));
                },
            .refresh = [router = router_.get(), instance_id = instance.id](
                           const auto& success,
                           const auto& error) { static_cast<void>(router->refresh(instance_id, success, error)); },
            .models = [router = router_.get(),
                       instance_id = instance.id]() -> const std::vector<holonight_domain::ModelId>& {
              static const std::vector<holonight_domain::ModelId> empty;
              const auto* models = router->availableModels(instance_id);
              return models == nullptr ? empty : *models;
            },
            .persist_models = [] {}});
    runtime_coordinator_->prepare(instance.id);
  }
}

UtilitySettingsController::~UtilitySettingsController() = default;

QVariantList UtilitySettingsController::providerInstances() const {
  QVariantList instances;
  instances.append(
      QVariantMap{{QStringLiteral("provider_id"), QString()}, {QStringLiteral("display_name"), tr("Use chat model")}});
  for (const auto& instance : provider_state_.instances) {
    if (!instance.enabled) {
      continue;
    }
    instances.append(QVariantMap{{QStringLiteral("provider_id"), instance.id},
                                 {QStringLiteral("display_name"), instance.display_name}});
  }
  return instances;
}

QString UtilitySettingsController::defaultProviderId() const {
  return draft_.default_utility_model ? draft_.default_utility_model->provider_id : QString();
}

void UtilitySettingsController::setDefaultProviderId(const QString& provider_id) {
  if (defaultProviderId() == provider_id) {
    return;
  }
  draft_.default_utility_model =
      provider_id.isEmpty()
          ? std::nullopt
          : std::make_optional(holonight_domain::ModelId{.provider_id = provider_id, .model_name = QString()});
  if (!provider_id.isEmpty()) {
    refreshModelsForInstance(provider_id);
  }
  emit defaultModelNamesChanged();
  emit draftChanged();
}

QString UtilitySettingsController::defaultModelName() const {
  return draft_.default_utility_model ? draft_.default_utility_model->model_name : QString();
}

void UtilitySettingsController::setDefaultModelName(const QString& model_name) {
  if (!draft_.default_utility_model || draft_.default_utility_model->model_name == model_name) {
    return;
  }
  draft_.default_utility_model->model_name = model_name;
  emit draftChanged();
}

QStringList UtilitySettingsController::defaultModelNames() const {
  const QString providerId = defaultProviderId();
  if (providerId.isEmpty()) {
    return {};
  }
  const auto* models = router_->availableModels(providerId);
  if (models == nullptr) {
    return {};
  }
  QStringList names;
  for (const auto& model : *models) {
    names.append(model.model_name);
  }
  return names;
}

bool UtilitySettingsController::chatTitleGenerationEnabled() const {
  return draft_.chat_title_generation_enabled.value_or(true);
}

void UtilitySettingsController::setChatTitleGenerationEnabled(bool enabled) {
  if (chatTitleGenerationEnabled() == enabled) {
    return;
  }
  draft_.chat_title_generation_enabled = enabled;
  emit draftChanged();
}

QString UtilitySettingsController::titleOverrideProviderId() const {
  return draft_.chat_title_model_override ? draft_.chat_title_model_override->provider_id : QString();
}

void UtilitySettingsController::setTitleOverrideProviderId(const QString& provider_id) {
  if (titleOverrideProviderId() == provider_id) {
    return;
  }
  draft_.chat_title_model_override =
      provider_id.isEmpty()
          ? std::nullopt
          : std::make_optional(holonight_domain::ModelId{.provider_id = provider_id, .model_name = QString()});
  if (!provider_id.isEmpty()) {
    refreshModelsForInstance(provider_id);
  }
  emit titleOverrideModelNamesChanged();
  emit draftChanged();
}

QString UtilitySettingsController::titleOverrideModelName() const {
  return draft_.chat_title_model_override ? draft_.chat_title_model_override->model_name : QString();
}

void UtilitySettingsController::setTitleOverrideModelName(const QString& model_name) {
  if (!draft_.chat_title_model_override || draft_.chat_title_model_override->model_name == model_name) {
    return;
  }
  draft_.chat_title_model_override->model_name = model_name;
  emit draftChanged();
}

QStringList UtilitySettingsController::titleOverrideModelNames() const {
  const QString providerId = titleOverrideProviderId();
  if (providerId.isEmpty()) {
    return {};
  }
  const auto* models = router_->availableModels(providerId);
  if (models == nullptr) {
    return {};
  }
  QStringList names;
  for (const auto& model : *models) {
    names.append(model.model_name);
  }
  return names;
}

bool UtilitySettingsController::dirty() const { return !(draft_ == saved_); }

bool UtilitySettingsController::canSave() const { return dirty() && isDraftValid(); }

QString UtilitySettingsController::saveNotice() const { return save_notice_; }

QString UtilitySettingsController::saveNoticeStatus() const { return save_notice_status_; }

bool UtilitySettingsController::save() {
  reconcileDraftAgainstProviderState();
  if (!isDraftValid()) {
    setSaveNotice(tr("Select a model for each configured provider."), QStringLiteral("error"));
    emit draftChanged();
    return false;
  }

  const auto result = config_repository_.saveUtilityConfig(draft_);
  if (!result.has_value()) {
    setSaveNotice(tr("Failed to save: %1").arg(result.error()), QStringLiteral("error"));
    return false;
  }

  saved_ = draft_;
  auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chat != nullptr);
  if (auto* runner = chat->utilityTaskRunnerForSettings()) {
    runner->applyUtilityConfig(saved_);
  }
  setSaveNotice(tr("Saved"), QStringLiteral("success"));
  emit defaultModelNamesChanged();
  emit titleOverrideModelNamesChanged();
  emit draftChanged();
  return true;
}

void UtilitySettingsController::discardDraft() {
  draft_ = saved_;
  emit defaultModelNamesChanged();
  emit titleOverrideModelNamesChanged();
  emit draftChanged();
}

void UtilitySettingsController::refreshModelsForInstance(const QString& providerInstanceId) {
  if (providerInstanceId.isEmpty()) {
    return;
  }
  runtime_coordinator_->prepare(providerInstanceId);
}

void UtilitySettingsController::applyProviderState(holonight_config::ProviderState provider_state) {
  const bool preserve_draft = dirty();
  for (const auto& previous : provider_state_.instances) {
    if (std::ranges::find(provider_state.instances, previous.id, &holonight_config::ProviderInstanceConfig::id) ==
        provider_state.instances.end()) {
      static_cast<void>(runtime_coordinator_->unregisterProvider(previous.id));
      static_cast<void>(router_->remove(previous.id));
    }
  }

  auto http_client = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  for (const auto& instance : provider_state.instances) {
    const auto utility_instance = withUtilityGenerationParams(instance);
    if (!router_->contains(instance.id)) {
      if (!router_->add(utility_instance, http_client)) {
        continue;
      }
      runtime_coordinator_->registerProvider(
          instance.id, instance.display_name, credentialPolicy(instance.type),
          ProviderRuntimeOperations{
              .set_credential =
                  [router = router_.get(), instance_id = instance.id](const QString& value) {
                    static_cast<void>(router->setCredential(instance_id, value));
                  },
              .refresh = [router = router_.get(), instance_id = instance.id](
                             const auto& success,
                             const auto& error) { static_cast<void>(router->refresh(instance_id, success, error)); },
              .models = [router = router_.get(),
                         instance_id = instance.id]() -> const std::vector<holonight_domain::ModelId>& {
                static const std::vector<holonight_domain::ModelId> empty;
                const auto* models = router->availableModels(instance_id);
                return models == nullptr ? empty : *models;
              },
              .persist_models = [] {}});
    } else {
      static_cast<void>(router_->reconfigure(utility_instance));
      static_cast<void>(router_->setEnabled(instance.id, instance.enabled));
    }
    runtime_coordinator_->setEnabled(instance.id, instance.enabled);
    if (instance.enabled) {
      runtime_coordinator_->prepare(instance.id);
    }
  }

  provider_state_ = std::move(provider_state);
  saved_ = config_repository_.loadUtilityConfig();
  if (!preserve_draft) {
    draft_ = saved_;
  }
  reconcileDraftAgainstProviderState();
  emit providerInstancesChanged();
  emit defaultModelNamesChanged();
  emit titleOverrideModelNamesChanged();
  emit draftChanged();
}

void UtilitySettingsController::onProviderChanged(const QString& providerId) {
  if (providerId == defaultProviderId()) {
    emit defaultModelNamesChanged();
  }
  if (providerId == titleOverrideProviderId()) {
    emit titleOverrideModelNamesChanged();
  }
}

void UtilitySettingsController::setSaveNotice(QString text, QString status) {
  save_notice_ = std::move(text);
  save_notice_status_ = std::move(status);
  emit saveNoticeChanged();
}

void UtilitySettingsController::reconcileDraftAgainstProviderState() {
  const auto isEnabledInstance = [this](const QString& providerId) {
    const auto found =
        std::ranges::find(provider_state_.instances, providerId, &holonight_config::ProviderInstanceConfig::id);
    return found != provider_state_.instances.end() && found->enabled;
  };
  if (draft_.default_utility_model.has_value() && !isEnabledInstance(draft_.default_utility_model->provider_id)) {
    draft_.default_utility_model.reset();
  }
  if (draft_.chat_title_model_override.has_value() &&
      !isEnabledInstance(draft_.chat_title_model_override->provider_id)) {
    draft_.chat_title_model_override.reset();
  }
}

bool UtilitySettingsController::isDraftValid() const {
  const auto isValidModel = [this](const std::optional<holonight_domain::ModelId>& model) {
    if (!model.has_value()) {
      return true;
    }
    if (model->provider_id.isEmpty() || model->model_name.isEmpty()) {
      return false;
    }
    const auto instance =
        std::ranges::find(provider_state_.instances, model->provider_id, &holonight_config::ProviderInstanceConfig::id);
    if (instance == provider_state_.instances.end() || !instance->enabled) {
      return false;
    }
    const auto* models = router_->availableModels(model->provider_id);
    return models != nullptr && std::ranges::find(*models, *model) != models->end();
  };
  return isValidModel(draft_.default_utility_model) && isValidModel(draft_.chat_title_model_override);
}

}  // namespace holonight_application
