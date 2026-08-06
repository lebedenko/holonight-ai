#include "holonight_application/provider_management_controller.h"

#include "holonight_application/anthropic_provider_settings_controller.h"
#include "holonight_application/chat_view_model.h"
#include "holonight_application/google_provider_settings_controller.h"
#include "holonight_application/openai_provider_settings_controller.h"
#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_draft_committer.h"
#include "holonight_application/provider_draft_guard.h"
#include "holonight_application/provider_draft_session.h"
#include "holonight_application/provider_instance_deleter.h"
#include "holonight_application/provider_instance_registry.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "holonight_application/provider_settings_controller.h"
#include "holonight_application/utility_settings_controller.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QAbstractItemModel>
#include <QQmlEngine>

#include <algorithm>
#include <holonight_config/config_path.h>
#include <utility>

namespace holonight_application {

namespace {
std::optional<holonight_config::ProviderType> providerType(const QString& value) {
  return holonight_config::providerTypeFromString(value);
}
}  // namespace

ProviderManagementController* ProviderManagementController::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(js_engine)
  return new ProviderManagementController(qml_engine);
}

ProviderManagementController::ProviderManagementController(QQmlEngine* qml_engine, QObject* parent)
    : QObject(parent),
      qml_engine_(qml_engine),
      config_repository_(holonight_config::resolveConfigFilePath()),
      registry_(std::make_unique<ProviderInstanceRegistry>(config_repository_.loadProviderState())),
      router_(std::make_unique<ProviderAdapterRouter>()) {
  auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chat != nullptr);
  auto* credentials = chat->credentialStoreForSettings();
  committer_ = std::make_unique<ProviderDraftCommitter>(registry_.get(), credentials, config_repository_);
  guard_ = std::make_unique<ProviderDraftGuard>(registry_.get(), committer_.get());
  runtime_coordinator_ = std::make_unique<ProviderRuntimeCoordinator>(credentials);
  deleter_ = std::make_unique<ProviderInstanceDeleter>(registry_.get(), router_.get(), runtime_coordinator_.get(),
                                                       credentials, config_repository_);

  auto http_client = std::make_shared<holonight_providers::QtNetworkHttpClient>();
  for (const auto& instance : registry_->savedState().instances) {
    static_cast<void>(router_->add(instance, http_client));
  }

  connect(guard_.get(), &ProviderDraftGuard::confirmationRequested, this, [this] {
    navigation_prompt_visible_ = true;
    emit navigationPromptVisibleChanged();
  });
  connect(committer_.get(), &ProviderDraftCommitter::errorChanged, this, &ProviderManagementController::updateError);
  connect(committer_.get(), &ProviderDraftCommitter::saveCompleted, this, [this] {
    if (!draft_) {
      return;
    }
    const auto& config = draft_->editable();
    if (!router_->reconfigure(config)) {
      static_cast<void>(router_->add(config, std::make_shared<holonight_providers::QtNetworkHttpClient>()));
    }
    auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
    Q_ASSERT(chat != nullptr);
    chat->applyProviderState(registry_->savedState());
    qml_engine_->singletonInstance<UtilitySettingsController*>("HolonightChat", "UtilitySettingsController")
        ->applyProviderState(registry_->savedState());
  });
  connect(deleter_.get(), &ProviderInstanceDeleter::errorChanged, this, &ProviderManagementController::updateError);
  connect(router_.get(), &ProviderAdapterRouter::activeStreamCountChanged, this, [this](const QString& instance_id) {
    if (instance_id == selectedInstanceId()) {
      emit draftChanged();
    }
  });
}

ProviderManagementController::~ProviderManagementController() = default;

QAbstractItemModel* ProviderManagementController::instances() const { return registry_->instances(); }
QString ProviderManagementController::selectedInstanceId() const { return registry_->selectedSettingsInstanceId(); }
QString ProviderManagementController::selectedProviderType() const {
  return draft_ ? holonight_config::providerTypeToString(draft_->editable().type) : QString{};
}
QString ProviderManagementController::displayName() const {
  return draft_ ? draft_->editable().display_name : QString{};
}
void ProviderManagementController::setDisplayName(QString name) {
  if (draft_) {
    draft_->setDisplayName(std::move(name));
  }
}
bool ProviderManagementController::enabled() const { return draft_ && draft_->editable().enabled; }
void ProviderManagementController::setEnabled(bool enabled_value) {
  if (draft_) {
    draft_->setEnabled(enabled_value);
  }
}
bool ProviderManagementController::dirty() const { return draft_ && draft_->dirty(); }
bool ProviderManagementController::addition() const { return draft_ && draft_->isAddition(); }
bool ProviderManagementController::canSave() const {
  return draft_ && (draft_->isAddition() || draft_->dirty()) && nameValidationError().isEmpty();
}
bool ProviderManagementController::canDelete() const {
  return draft_ && !draft_->isAddition() && router_->canDelete(draft_->editable().id);
}
QString ProviderManagementController::nameValidationError() const {
  if (!draft_) {
    return {};
  }
  const QString name = draft_->editable().display_name.trimmed();
  if (name.isEmpty()) {
    return tr("Name is required.");
  }
  const QString normalized = name.toCaseFolded();
  for (const auto& candidate : registry_->savedState().instances) {
    if (candidate.id != draft_->editable().id && candidate.display_name.trimmed().toCaseFolded() == normalized) {
      return tr("Name must be unique.");
    }
  }
  return {};
}
QString ProviderManagementController::deletionExplanation() const {
  return draft_ && !draft_->isAddition() && !router_->canDelete(draft_->editable().id)
             ? tr("This provider cannot be deleted while it is streaming a response.")
             : QString{};
}
QStringList ProviderManagementController::validationErrors() const {
  return draft_ ? draft_->validationErrors() : QStringList{};
}
QString ProviderManagementController::error() const {
  return deleter_->error().isEmpty() ? committer_->error() : deleter_->error();
}
bool ProviderManagementController::navigationPromptVisible() const { return navigation_prompt_visible_; }

bool ProviderManagementController::addProvider(const QString& provider_type) {
  const auto type = providerType(provider_type);
  if (!type.has_value()) {
    return false;
  }
  const QString previous_selection = selectedInstanceId();
  guard_->requestNavigation([this, type] {
    const auto instance = registry_->addDraft(*type);
    if (!instance.has_value()) {
      return;
    }
    setDraft(std::make_unique<ProviderDraftSession>(
        std::nullopt, *instance, [this](const QString& name, const QString& current_instance_id) {
          Q_UNUSED(current_instance_id)
          const QString normalized = name.trimmed().toCaseFolded();
          if (normalized.isEmpty()) {
            return false;
          }
          return std::ranges::none_of(registry_->savedState().instances, [&normalized](const auto& candidate) {
            return candidate.display_name.trimmed().toCaseFolded() == normalized;
          });
        }));
  });
  return selectedInstanceId() != previous_selection;
}

void ProviderManagementController::requestSelection(const QString& instance_id) {
  guard_->requestNavigation([this, instance_id] { selectNow(instance_id); });
}

void ProviderManagementController::requestClose() {
  guard_->requestNavigation([this] { emit closeApproved(); });
}

bool ProviderManagementController::save() {
  const bool result = committer_->save(draft_.get());
  updateError();
  return result;
}

void ProviderManagementController::discardDraft() {
  if (!draft_) {
    return;
  }
  if (draft_->isAddition()) {
    if (!registry_->cancelDraft()) {
      return;
    }
    setDraft(nullptr);
    return;
  }
  draft_->discard();
}

void ProviderManagementController::discardAndContinue() {
  navigation_prompt_visible_ = false;
  emit navigationPromptVisibleChanged();
  static_cast<void>(guard_->discardAndContinue());
}

void ProviderManagementController::saveAndContinue() {
  if (guard_->saveAndContinue()) {
    navigation_prompt_visible_ = false;
    emit navigationPromptVisibleChanged();
  }
  updateError();
}

void ProviderManagementController::cancelNavigation() {
  guard_->cancelNavigation();
  navigation_prompt_visible_ = false;
  emit navigationPromptVisibleChanged();
}

bool ProviderManagementController::deleteSelected(bool confirmed) {
  if (!draft_ || draft_->isAddition()) {
    return false;
  }
  const QString instance_id = draft_->editable().id;
  if (!deleter_->remove(instance_id, confirmed)) {
    updateError();
    return false;
  }
  auto* chat = qml_engine_->singletonInstance<ChatViewModel*>("HolonightChat", "ChatViewModel");
  Q_ASSERT(chat != nullptr);
  chat->applyProviderState(registry_->savedState());
  qml_engine_->singletonInstance<UtilitySettingsController*>("HolonightChat", "UtilitySettingsController")
      ->applyProviderState(registry_->savedState());
  setDraft(nullptr);
  emit selectionChanged();
  updateError();
  return true;
}

void ProviderManagementController::selectNow(const QString& instance_id) {
  if (!registry_->selectSettingsInstance(instance_id)) {
    return;
  }
  const auto* instance = registry_->instances()->find(instance_id);
  if (instance == nullptr) {
    return;
  }
  const auto original = *instance;
  setDraft(std::make_unique<ProviderDraftSession>(
      original, original, [this](const QString& name, const QString& current_instance_id) {
        const QString normalized = name.trimmed().toCaseFolded();
        if (normalized.isEmpty()) {
          return false;
        }
        const bool duplicate = std::ranges::any_of(registry_->savedState().instances, [&](const auto& candidate) {
          return candidate.id != current_instance_id && candidate.display_name.trimmed().toCaseFolded() == normalized;
        });
        return !duplicate;
      }));
}

void ProviderManagementController::setDraft(std::unique_ptr<ProviderDraftSession> draft) {
  draft_ = std::move(draft);
  guard_->setDraftSession(draft_.get());
  if (draft_) {
    connect(draft_.get(), &ProviderDraftSession::changed, this, &ProviderManagementController::draftChanged);
  }
  retargetProviderController();
  emit selectionChanged();
  emit draftChanged();
}

void ProviderManagementController::retargetProviderController() {
  if (!draft_) {
    return;
  }
  switch (draft_->editable().type) {
    case holonight_config::ProviderType::Ollama:
      qml_engine_->singletonInstance<ProviderSettingsController*>("HolonightChat", "ProviderSettingsController")
          ->setDraftSession(draft_.get());
      break;
    case holonight_config::ProviderType::OpenAi:
      qml_engine_
          ->singletonInstance<OpenAIProviderSettingsController*>("HolonightChat", "OpenAIProviderSettingsController")
          ->setDraftSession(draft_.get());
      break;
    case holonight_config::ProviderType::Anthropic:
      qml_engine_
          ->singletonInstance<AnthropicProviderSettingsController*>("HolonightChat",
                                                                    "AnthropicProviderSettingsController")
          ->setDraftSession(draft_.get());
      break;
    case holonight_config::ProviderType::Google:
      qml_engine_
          ->singletonInstance<GoogleProviderSettingsController*>("HolonightChat", "GoogleProviderSettingsController")
          ->setDraftSession(draft_.get());
      break;
  }
}

void ProviderManagementController::updateError() { emit errorChanged(); }

}  // namespace holonight_application
