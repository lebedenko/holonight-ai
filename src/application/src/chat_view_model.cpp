#include "holonight_application/chat_view_model.h"

#include "holonight_application/chat_controller.h"
#include "holonight_application/tools/list_files_tool.h"
#include "holonight_providers/qt_network_http_client.h"

#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <holonight_config/config_path.h>
#include <holonight_config/config_repository.h>
#include <holonight_credentials/secret_service_credential_store.h>
#include <holonight_persistence/database_path.h>
#include <holonight_persistence/sqlite_conversation_repository.h>
#include <utility>
#include <variant>

namespace holonight_application {

using holonight_domain::Conversation;
using holonight_domain::ConversationId;
using holonight_domain::ModelId;
using holonight_domain::StreamEvent;
using holonight_persistence::ConversationSummary;
using holonight_persistence::LoadedConversation;
using holonight_providers::AnthropicProvider;
using holonight_providers::GoogleProvider;
using holonight_providers::OllamaProvider;
using holonight_providers::OpenAIProvider;
using holonight_providers::QtNetworkHttpClient;

namespace {

std::vector<ModelId> cachedModels(const QString& provider_id, const QStringList& model_names) {
  std::vector<ModelId> models;
  models.reserve(static_cast<std::size_t>(model_names.size()));
  for (const QString& model_name : model_names) {
    models.push_back(ModelId{.provider_id = provider_id, .model_name = model_name});
  }
  return models;
}

QString configuredDefaultModel(const holonight_config::ProviderInstanceConfig& instance) {
  return std::visit([](const auto& settings) { return settings.default_model; }, instance.settings);
}

CredentialPolicy credentialPolicy(holonight_config::ProviderType type) {
  return type == holonight_config::ProviderType::Ollama ? CredentialPolicy::Optional : CredentialPolicy::Required;
}

template <typename Config>
Config firstProviderConfig(const holonight_config::ProviderState& state, holonight_config::ProviderType type) {
  const auto instance = std::ranges::find_if(
      state.instances,
      [type](const holonight_config::ProviderInstanceConfig& candidate) { return candidate.type == type; });
  if (instance == state.instances.end()) {
    return {};
  }
  if (const auto* config = std::get_if<Config>(&instance->settings)) {
    return *config;
  }
  return {};
}

}  // namespace

ChatViewModel* ChatViewModel::create(QQmlEngine* qml_engine, QJSEngine* js_engine) {
  Q_UNUSED(qml_engine)
  Q_UNUSED(js_engine)
  const holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());
  const holonight_config::UtilityConfig utilityConfig = configRepository.loadUtilityConfig();
  const holonight_config::ProviderState providerState = configRepository.loadProviderState();
  const auto ollamaConfig = firstProviderConfig<holonight_config::OllamaProviderConfig>(
      providerState, holonight_config::ProviderType::Ollama);
  const auto openAiConfig = firstProviderConfig<holonight_config::OpenAIProviderConfig>(
      providerState, holonight_config::ProviderType::OpenAi);
  const auto anthropicConfig = firstProviderConfig<holonight_config::AnthropicProviderConfig>(
      providerState, holonight_config::ProviderType::Anthropic);
  const auto googleConfig = firstProviderConfig<holonight_config::GoogleProviderConfig>(
      providerState, holonight_config::ProviderType::Google);

  // REQ-F-001/REQ-F-009. The one process-wide ToolRegistry: registered tools are shared between
  // adapterRouter (advertises them to Anthropic via toAnthropicToolsArray() when an instance has
  // tool_calling_enabled) and the ChatController this constructs below (executes them via
  // invoke()) -- both must agree on what "the tools" are.
  auto toolRegistry = std::make_shared<ToolRegistry>();
  toolRegistry->registerTool(listFilesToolRegistration());

  auto adapterRouter = std::make_unique<ProviderAdapterRouter>(toolRegistry);
  for (const auto& instance : providerState.instances) {
    auto client = std::make_shared<QtNetworkHttpClient>();
    if (adapterRouter->add(instance, client)) {
      static_cast<void>(adapterRouter->restoreAvailableModels(
          instance.id, cachedModels(instance.id, configRepository.loadCachedModels(instance.id))));
    }
  }

  auto ollamaHttpClient = std::make_shared<QtNetworkHttpClient>();
  auto ollamaProvider = std::make_shared<OllamaProvider>(std::move(ollamaHttpClient), ollamaConfig.base_url);
  ollamaProvider->restoreAvailableModels(
      cachedModels(QStringLiteral("ollama"), configRepository.loadCachedModels(QStringLiteral("ollama"))));
  ollamaProvider->setContextWindow(ollamaConfig.context_window);
  ollamaProvider->setTemperature(ollamaConfig.temperature);

  auto openAiHttpClient = std::make_shared<QtNetworkHttpClient>();
  auto openAiProvider = std::make_shared<OpenAIProvider>(std::move(openAiHttpClient), openAiConfig.base_url);
  openAiProvider->restoreAvailableModels(
      cachedModels(QStringLiteral("openai"), configRepository.loadCachedModels(QStringLiteral("openai"))));
  openAiProvider->setTemperature(openAiConfig.temperature);

  auto anthropicHttpClient = std::make_shared<QtNetworkHttpClient>();
  auto anthropicProvider =
      std::make_shared<AnthropicProvider>(std::move(anthropicHttpClient), anthropicConfig.base_url);
  anthropicProvider->restoreAvailableModels(
      cachedModels(QStringLiteral("anthropic"), configRepository.loadCachedModels(QStringLiteral("anthropic"))));
  anthropicProvider->setTemperature(anthropicConfig.temperature);
  anthropicProvider->setMaxOutputTokens(anthropicConfig.max_output_tokens);

  auto googleHttpClient = std::make_shared<QtNetworkHttpClient>();
  auto googleProvider = std::make_shared<GoogleProvider>(std::move(googleHttpClient), googleConfig.base_url);
  googleProvider->restoreAvailableModels(
      cachedModels(QStringLiteral("google"), configRepository.loadCachedModels(QStringLiteral("google"))));
  googleProvider->setTemperature(googleConfig.temperature);
  googleProvider->setMaxOutputTokens(googleConfig.max_output_tokens);

  auto repository = std::make_unique<holonight_persistence::SqliteConversationRepository>(
      holonight_persistence::resolveDatabaseFilePath());
  auto credentialStore = std::make_unique<holonight_credentials::SecretServiceCredentialStore>();
  return new ChatViewModel(std::move(ollamaProvider), std::move(openAiProvider), std::move(anthropicProvider),
                           std::move(googleProvider), std::move(repository),
                           ProviderDefaultModels{.ollama = ollamaConfig.default_model,
                                                 .openai = openAiConfig.default_model,
                                                 .anthropic = anthropicConfig.default_model,
                                                 .google = googleConfig.default_model},
                           nullptr, std::move(credentialStore), utilityConfig,
                           UtilityProviderEndpoints{.ollama_base_url = ollamaConfig.base_url,
                                                    .openai_base_url = openAiConfig.base_url,
                                                    .anthropic_base_url = anthropicConfig.base_url,
                                                    .google_base_url = googleConfig.base_url},
                           providerState, std::move(adapterRouter), std::move(toolRegistry));
}

ChatViewModel::ChatViewModel(
    std::shared_ptr<OllamaProvider> ollama_provider, std::shared_ptr<OpenAIProvider> openai_provider,
    std::shared_ptr<AnthropicProvider> anthropic_provider, std::shared_ptr<GoogleProvider> google_provider,
    std::unique_ptr<holonight_persistence::ConversationRepository> repository, ProviderDefaultModels default_models,
    QObject* parent, std::unique_ptr<holonight_credentials::CredentialStore> credential_store,
    holonight_config::UtilityConfig utility_config, const UtilityProviderEndpoints& utility_endpoints,
    holonight_config::ProviderState provider_state, std::unique_ptr<ProviderAdapterRouter> adapter_router,
    std::shared_ptr<ToolRegistry> tool_registry)
    : QObject(parent),
      ollama_provider_(std::move(ollama_provider)),
      openai_provider_(std::move(openai_provider)),
      anthropic_provider_(std::move(anthropic_provider)),
      google_provider_(std::move(google_provider)),
      repository_(std::move(repository)),
      credential_store_(std::move(credential_store)),
      adapter_router_(std::move(adapter_router)),
      tool_registry_(tool_registry ? std::move(tool_registry) : std::make_shared<ToolRegistry>()),
      chat_controller_(adapter_router_ ? std::make_unique<ChatController>(
                                             adapter_router_.get(),
                                             std::make_shared<holonight_providers::SteadyClock>(), tool_registry_)
                                       : std::make_unique<ChatController>(
                                             ollama_provider_, openai_provider_, anthropic_provider_, google_provider_,
                                             std::make_shared<holonight_providers::SteadyClock>(), tool_registry_)),
      default_models_(std::move(default_models)),
      provider_state_(std::move(provider_state)),
      message_model_(new MessageListModel(provider_state_, tool_registry_, this)),
      conversation_list_model_(new ConversationListModel(this)) {
  for (const auto& instance : provider_state_.instances) {
    instance_default_models_.insert(instance.id, configuredDefaultModel(instance));
    instance_models_loaded_.insert(instance.id, adapter_router_ != nullptr &&
                                                    adapter_router_->availableModels(instance.id) != nullptr &&
                                                    !adapter_router_->availableModels(instance.id)->empty());
  }
  if (credential_store_) {
    provider_runtime_coordinator_ = std::make_unique<ProviderRuntimeCoordinator>(credential_store_.get());
    const auto configPath = holonight_config::resolveConfigFilePath();
    auto persistModels = [configPath](const QString& provider_id, const auto& provider) {
      QStringList names;
      for (const ModelId& model : provider->availableModels()) {
        names.append(model.model_name);
      }
      holonight_config::ConfigRepository config(configPath);
      (void)config.saveCachedModels(provider_id, names);
    };
    if (adapter_router_) {
      for (const auto& instance : provider_state_.instances) {
        provider_runtime_coordinator_->registerProvider(
            instance.id, instance.display_name, credentialPolicy(instance.type),
            ProviderRuntimeOperations{
                .set_credential =
                    [router = adapter_router_.get(), instance_id = instance.id](const QString& value) {
                      static_cast<void>(router->setCredential(instance_id, value));
                    },
                .refresh = [router = adapter_router_.get(), instance_id = instance.id](
                               const auto& success,
                               const auto& error) { static_cast<void>(router->refresh(instance_id, success, error)); },
                .models = [router = adapter_router_.get(), instance_id = instance.id]() -> const std::vector<ModelId>& {
                  static const std::vector<ModelId> empty;
                  const auto* models = router->availableModels(instance_id);
                  return models == nullptr ? empty : *models;
                },
                .persist_models =
                    [router = adapter_router_.get(), instance_id = instance.id] {
                      const auto* models = router->availableModels(instance_id);
                      if (models == nullptr) {
                        return;
                      }
                      QStringList names;
                      for (const auto& model : *models) {
                        names.append(model.model_name);
                      }
                      holonight_config::ConfigRepository config(holonight_config::resolveConfigFilePath());
                      static_cast<void>(config.saveCachedModels(instance_id, names));
                    }});
        provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
      }
    } else {
      provider_runtime_coordinator_->registerProvider(
          QStringLiteral("ollama"), QStringLiteral("Ollama"), CredentialPolicy::Optional,
          ProviderRuntimeOperations{
              .set_credential = [provider = ollama_provider_](const QString& value) { provider->setAuthToken(value); },
              .refresh = [provider = ollama_provider_](const auto& success,
                                                       const auto& error) { provider->refresh(success, error); },
              .models = [provider = ollama_provider_]() -> const std::vector<ModelId>& {
                return provider->availableModels();
              },
              .persist_models = [persistModels,
                                 provider = ollama_provider_] { persistModels(QStringLiteral("ollama"), provider); }});
      provider_runtime_coordinator_->registerProvider(
          QStringLiteral("openai"), QStringLiteral("OpenAI"), CredentialPolicy::Required,
          ProviderRuntimeOperations{
              .set_credential = [provider = openai_provider_](const QString& value) { provider->setAuthToken(value); },
              .refresh = [provider = openai_provider_](const auto& success,
                                                       const auto& error) { provider->refresh(success, error); },
              .models = [provider = openai_provider_]() -> const std::vector<ModelId>& {
                return provider->availableModels();
              },
              .persist_models = [persistModels,
                                 provider = openai_provider_] { persistModels(QStringLiteral("openai"), provider); }});
      provider_runtime_coordinator_->registerProvider(
          QStringLiteral("anthropic"), QStringLiteral("Anthropic"), CredentialPolicy::Required,
          ProviderRuntimeOperations{
              .set_credential = [provider = anthropic_provider_](const QString& value) { provider->setAuthKey(value); },
              .refresh = [provider = anthropic_provider_](const auto& success,
                                                          const auto& error) { provider->refresh(success, error); },
              .models = [provider = anthropic_provider_]() -> const std::vector<ModelId>& {
                return provider->availableModels();
              },
              .persist_models = [persistModels,
                                 provider =
                                     anthropic_provider_] { persistModels(QStringLiteral("anthropic"), provider); }});
      provider_runtime_coordinator_->registerProvider(
          QStringLiteral("google"), QStringLiteral("Google"), CredentialPolicy::Required,
          ProviderRuntimeOperations{
              .set_credential = [provider = google_provider_](const QString& value) { provider->setAuthKey(value); },
              .refresh = [provider = google_provider_](const auto& success,
                                                       const auto& error) { provider->refresh(success, error); },
              .models = [provider = google_provider_]() -> const std::vector<ModelId>& {
                return provider->availableModels();
              },
              .persist_models = [persistModels,
                                 provider = google_provider_] { persistModels(QStringLiteral("google"), provider); }});
    }
    connect(provider_runtime_coordinator_.get(), &ProviderRuntimeCoordinator::providerChanged, this,
            &ChatViewModel::onProviderChanged);
    if (adapter_router_) {
      for (const auto& instance : provider_state_.instances) {
        provider_runtime_coordinator_->prepare(instance.id);
      }
    } else {
      for (const QString& providerId : {QStringLiteral("ollama"), QStringLiteral("openai"), QStringLiteral("anthropic"),
                                        QStringLiteral("google")}) {
        provider_runtime_coordinator_->prepare(providerId);
      }
    }

    QHash<QString, std::vector<ModelId>> utilityModels;
    if (adapter_router_) {
      for (const auto& instance : provider_state_.instances) {
        const auto* models = adapter_router_->availableModels(instance.id);
        if (models != nullptr) {
          utilityModels.insert(instance.id, *models);
        }
      }
    }
    utility_task_runner_ = std::make_unique<UtilityTaskRunner>(credential_store_.get(), std::move(utility_config),
                                                               utility_endpoints, this, provider_state_, utilityModels);
    connect(utility_task_runner_.get(), &UtilityTaskRunner::titleGenerated, this, &ChatViewModel::onTitleGenerated);
    connect(utility_task_runner_.get(), &UtilityTaskRunner::titleGenerationStarted, this,
            &ChatViewModel::onTitleGenerationStarted);
    connect(utility_task_runner_.get(), &UtilityTaskRunner::titleGenerationFinished, this,
            &ChatViewModel::onTitleGenerationFinished);
  }
  syncAvailableModels();
  if (!provider_runtime_coordinator_ && !adapter_router_) {
    ollama_provider_->refresh(
        [this] {
          ollama_models_loaded_ = true;
          syncAvailableModels();
        },
        [this](const QString& /*reason*/) { syncAvailableModels(); });
  }

  // OpenAI/Anthropic/Google model discovery starts in their respective *ProviderSettingsController
  // after asynchronous credential retrieval. Refreshing here would always race with an unset token
  // and emit a needless 401.

  connect(repository_.get(), &holonight_persistence::ConversationRepository::initialized, this,
          &ChatViewModel::onRepositoryInitialized);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::unavailable, this,
          &ChatViewModel::onRepositoryUnavailable);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationListLoaded, this,
          &ChatViewModel::onConversationListLoaded);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationCreated, this,
          &ChatViewModel::onConversationCreated);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationLoaded, this,
          &ChatViewModel::onConversationLoaded);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationRenamed, this,
          &ChatViewModel::onConversationRenamed);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationDeleted, this,
          &ChatViewModel::onConversationDeleted);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationPinned, this,
          &ChatViewModel::onConversationPinned);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::conversationUnpinned, this,
          &ChatViewModel::onConversationUnpinned);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::lastModelIdUpdated, this,
          &ChatViewModel::onLastModelIdUpdated);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::usageForConversationLoaded, this,
          &ChatViewModel::onUsageForConversationLoaded);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::usagePersisted, this,
          &ChatViewModel::onUsagePersisted);
  connect(repository_.get(), &holonight_persistence::ConversationRepository::error, this,
          &ChatViewModel::onRepositoryError);

  repository_->initialize();
}

ChatViewModel::~ChatViewModel() { stop(); }

QVariantList ChatViewModel::availableProviders() const { return available_providers_; }

QStringList ChatViewModel::availableModelNames() const { return available_model_names_; }

QString ChatViewModel::selectedProviderId() const { return selected_model_id_.provider_id; }

void ChatViewModel::setSelectedProviderId(const QString& provider_id) {
  if (provider_id == selected_model_id_.provider_id || modelNames(provider_id).isEmpty()) {
    return;
  }
  const QStringList names = modelNames(provider_id);
  QString model_name = remembered_models_.value(provider_id);
  if (!names.contains(model_name)) {
    model_name = defaultModelName(provider_id);
  }
  if (!names.contains(model_name)) {
    model_name = names.front();
  }
  applySelection(ModelId{.provider_id = provider_id, .model_name = model_name});
  prepareSelectedProvider();
  refreshComputedProperties();
}

QString ChatViewModel::selectedModelName() const { return selected_model_id_.model_name; }

void ChatViewModel::setSelectedModelName(const QString& model_name) {
  if (model_name == selected_model_id_.model_name || !available_model_names_.contains(model_name)) {
    return;
  }
  applySelection(ModelId{.provider_id = selected_model_id_.provider_id, .model_name = model_name});
  refreshComputedProperties();
}

ChatViewModel::ProviderStatus ChatViewModel::selectedProviderStatus() const { return selected_provider_status_; }

const ModelId& ChatViewModel::selectedModel() const { return selected_model_id_; }

bool ChatViewModel::canSend() const { return cached_can_send_; }

bool ChatViewModel::canRegenerate() const { return cached_can_regenerate_; }

bool ChatViewModel::isStreaming() const { return cached_is_streaming_; }

QString ChatViewModel::errorMessage() const { return error_message_; }
QString ChatViewModel::providerStatusMessage() const { return provider_status_message_; }

QString ChatViewModel::inputText() const { return input_text_; }

void ChatViewModel::setInputText(QString text) {
  if (input_text_ == text) {
    return;
  }
  input_text_ = std::move(text);
  emit inputTextChanged();
}

MessageListModel* ChatViewModel::messages() const { return message_model_; }

bool ChatViewModel::messagesReady() const { return messages_ready_; }

ConversationListModel* ChatViewModel::conversationList() const { return conversation_list_model_; }

QString ChatViewModel::activeConversationId() const { return active_conversation_id_; }

QString ChatViewModel::persistenceStatusMessage() const { return persistence_status_message_; }

Conversation* ChatViewModel::conversation() const { return conversation_.get(); }

std::shared_ptr<OllamaProvider> ChatViewModel::providerForSettings() const { return ollama_provider_; }

std::shared_ptr<OpenAIProvider> ChatViewModel::openAiProviderForSettings() const { return openai_provider_; }

std::shared_ptr<AnthropicProvider> ChatViewModel::anthropicProviderForSettings() const { return anthropic_provider_; }

std::shared_ptr<GoogleProvider> ChatViewModel::googleProviderForSettings() const { return google_provider_; }
holonight_credentials::CredentialStore* ChatViewModel::credentialStoreForSettings() const {
  return credential_store_.get();
}
UtilityTaskRunner* ChatViewModel::utilityTaskRunnerForSettings() const { return utility_task_runner_.get(); }

ProviderRuntimeCoordinator* ChatViewModel::providerRuntimeCoordinator() const {
  return provider_runtime_coordinator_.get();
}

void ChatViewModel::applyProviderState(holonight_config::ProviderState provider_state) {
  if (!adapter_router_) {
    return;
  }

  for (const auto& previous : provider_state_.instances) {
    const auto current =
        std::ranges::find(provider_state.instances, previous.id, &holonight_config::ProviderInstanceConfig::id);
    if (current == provider_state.instances.end()) {
      if (provider_runtime_coordinator_) {
        static_cast<void>(provider_runtime_coordinator_->unregisterProvider(previous.id));
      }
      static_cast<void>(adapter_router_->remove(previous.id));
      instance_default_models_.remove(previous.id);
      instance_models_loaded_.remove(previous.id);
      remembered_models_.remove(previous.id);
    }
  }

  const holonight_config::ConfigRepository configRepository(holonight_config::resolveConfigFilePath());
  for (const auto& instance : provider_state.instances) {
    if (!adapter_router_->contains(instance.id)) {
      auto client = std::make_shared<QtNetworkHttpClient>();
      if (adapter_router_->add(instance, client)) {
        static_cast<void>(adapter_router_->restoreAvailableModels(
            instance.id, cachedModels(instance.id, configRepository.loadCachedModels(instance.id))));
        registerRuntimeProvider(instance);
      }
    } else {
      static_cast<void>(adapter_router_->reconfigure(instance));
      static_cast<void>(adapter_router_->setEnabled(instance.id, instance.enabled));
      if (provider_runtime_coordinator_) {
        provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
      }
    }
    instance_default_models_.insert(instance.id, configuredDefaultModel(instance));
  }

  provider_state_ = std::move(provider_state);
  message_model_->setProviderState(provider_state_);
  syncAvailableModels();
  if (utility_task_runner_) {
    utility_task_runner_->applyProviderState(provider_state_);
  }
}

void ChatViewModel::send(const QString& text) {
  if (!canSend() || text.trimmed().isEmpty()) {
    return;
  }

  const bool isFirstMessage = conversation_->messages().empty();

  QPointer<ChatViewModel> guard(this);
  const auto result =
      chat_controller_->send(*conversation_, selected_model_id_, text, [guard](const StreamEvent& event) {
        if (auto* self = guard.data()) {
          self->onStreamEvent(event);
        }
      });
  if (!result.has_value()) {
    return;
  }

  const auto& conversationMessages = conversation_->messages();
  const holonight_domain::Message& userMessage = conversationMessages[conversationMessages.size() - 2];
  const holonight_domain::Message& assistantMessage = conversationMessages[conversationMessages.size() - 1];
  message_model_->insertNewestMessage(userMessage);
  message_model_->insertNewestMessage(assistantMessage);
  synced_message_count_ = conversation_->messages().size();

  if (persistence_enabled_ && conversation_materialized_) {
    const QString conversationId = conversation_->id().toString();
    repository_->persistNewMessage(conversationId, userMessage);
    repository_->persistNewMessage(conversationId, assistantMessage);
    repository_->updateLastModelId(conversationId, selected_model_id_);
  }

  if (isFirstMessage && conversation_->title() == holonight_persistence::kDefaultConversationTitle) {
    const QString title = holonight_domain::deriveConversationTitle(text);
    conversation_->setTitle(title);
    if (persistence_enabled_ && conversation_materialized_) {
      repository_->renameConversation(conversation_->id().toString(), title, holonight_domain::TitleSource::Fallback);
    }
  }

  if (persistence_enabled_ && !conversation_materialized_) {
    conversation_materialized_ = true;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    repository_->materializeConversation(
        ConversationSummary{
            .id = conversation_->id().toString(),
            .title = conversation_->title(),
            .created_at = conversation_->createdAt(),
            .updated_at = now,
            .last_model_id = selected_model_id_,
            .title_source = holonight_domain::TitleSource::Fallback,
        },
        conversation_->messages());
  }

  if (persistence_enabled_) {
    persisted_message_count_ = conversation_->messages().size();
  }

  setInputText(QString());
  setErrorMessage(QString());
  refreshComputedProperties();
}

void ChatViewModel::stop() {
  if (!chat_controller_ || !conversation_) {
    return;
  }
  chat_controller_->stop(conversation_->id());
  refreshComputedProperties();
}

void ChatViewModel::regenerate() {
  if (!canRegenerate()) {
    return;
  }

  QPointer<ChatViewModel> guard(this);
  const auto result =
      chat_controller_->regenerate(*conversation_, selected_model_id_, [guard](const StreamEvent& event) {
        if (auto* self = guard.data()) {
          self->onStreamEvent(event);
        }
      });
  if (!result.has_value()) {
    return;
  }

  message_model_->updateNewestMessage(conversation_->messages().back());

  if (persistence_enabled_) {
    repository_->updateLastModelId(conversation_->id().toString(), selected_model_id_);
  }

  setErrorMessage(QString());
  refreshComputedProperties();
}

void ChatViewModel::createConversation() {
  if (!conversation_materialized_ && conversation_ && conversation_->messages().empty()) {
    return;
  }
  stop();
  adoptTransientDraft();
}

void ChatViewModel::switchConversation(const QString& conversationId) {
  if (conversationId == active_conversation_id_) {
    return;
  }
  stop();
  if (persistence_enabled_) {
    setMessagesReady(false);
    repository_->loadConversation(conversationId);
  }
}

void ChatViewModel::renameConversation(const QString& conversationId, const QString& newTitle) {
  if (persistence_enabled_) {
    repository_->renameConversation(conversationId, newTitle);
    return;
  }
  if (conversationId == active_conversation_id_ && conversation_) {
    conversation_->setTitle(newTitle);
  }
}

void ChatViewModel::deleteConversation(const QString& conversationId) {
  const bool wasActive = (conversationId == active_conversation_id_);
  if (wasActive) {
    stop();
  }
  if (persistence_enabled_) {
    if (wasActive) {
      setMessagesReady(false);
    }
    repository_->deleteConversation(conversationId);
  }
}

void ChatViewModel::pinConversation(const QString& conversationId) {
  if (persistence_enabled_) {
    repository_->pinConversation(conversationId);
  }
}

void ChatViewModel::unpinConversation(const QString& conversationId) {
  if (persistence_enabled_) {
    repository_->unpinConversation(conversationId);
  }
}

void ChatViewModel::dismissPersistenceBanner() { setPersistenceStatusMessage(QString()); }

QString ChatViewModel::selectModelNameForProvider(const QString& provider_id) const {
  const QStringList nextNames = modelNames(provider_id);
  QString remembered = remembered_models_.value(provider_id);

  if (nextNames.contains(remembered)) {
    return remembered;
  }
  if (nextNames.contains(defaultModelName(provider_id))) {
    return defaultModelName(provider_id);
  }
  return nextNames.front();
}

void ChatViewModel::syncAvailableModels(const std::optional<ModelId>& preferredModelId) {
  QVariantList providers;
  if (adapter_router_) {
    for (const auto& instance : provider_state_.instances) {
      if (instance.enabled && !modelNames(instance.id).isEmpty()) {
        providers.append(QVariantMap{{QStringLiteral("provider_id"), instance.id},
                                     {QStringLiteral("display_name"), instance.display_name}});
      }
    }
  } else {
    const QStringList providerIds{QStringLiteral("ollama"), QStringLiteral("openai"), QStringLiteral("anthropic"),
                                  QStringLiteral("google")};
    const QStringList displayNames{QStringLiteral("Ollama"), QStringLiteral("OpenAI"), QStringLiteral("Anthropic"),
                                   QStringLiteral("Google")};
    for (qsizetype index = 0; index < providerIds.size(); ++index) {
      if (!modelNames(providerIds[index]).isEmpty()) {
        providers.append(QVariantMap{{QStringLiteral("provider_id"), providerIds[index]},
                                     {QStringLiteral("display_name"), displayNames[index]}});
      }
    }
  }

  if (providers != available_providers_) {
    available_providers_ = providers;
    emit availableProvidersChanged();
  }

  ModelId next = validateModelSelection(preferredModelId, providers);
  applySelection(next);

  if (providers.isEmpty()) {
    setErrorMessage(QStringLiteral("No models available — check provider settings and credentials"));
  } else {
    setErrorMessage(QString());
  }

  refreshComputedProperties();
}

holonight_domain::ModelId ChatViewModel::validateModelSelection(const std::optional<ModelId>& preferredModelId,
                                                                const QVariantList& providers) const {
  ModelId next = selected_model_id_;

  // Case 1: Preferred model is valid
  if (preferredModelId.has_value() &&
      modelNames(preferredModelId->provider_id).contains(preferredModelId->model_name)) {
    return *preferredModelId;
  }

  // Case 2: No provider currently selected
  if (next.provider_id.isEmpty()) {
    return selectFirstAvailableModel(providers);
  }

  // Case 3: Current provider/model is no longer valid
  if ((adapter_router_ || providerHasReported(next.provider_id)) &&
      !modelNames(next.provider_id).contains(next.model_name)) {
    return selectReplacementModel(next, providers);
  }

  return next;
}

holonight_domain::ModelId ChatViewModel::selectFirstAvailableModel(const QVariantList& providers) const {
  if (providers.isEmpty()) {
    return {};
  }

  ModelId result;
  result.provider_id = providers.front().toMap().value(QStringLiteral("provider_id")).toString();
  result.model_name = selectModelNameForProvider(result.provider_id);
  return result;
}

holonight_domain::ModelId ChatViewModel::selectReplacementModel(const ModelId& current,
                                                                const QVariantList& providers) const {
  const QStringList names = modelNames(current.provider_id);

  // Current provider has no models, pick first available
  if (names.isEmpty()) {
    return selectFirstAvailableModel(providers);
  }

  // Current provider still has models but missing the previously selected one
  ModelId result = current;
  result.model_name = selectModelNameForProvider(result.provider_id);
  return result;
}

QString ChatViewModel::providerDisplayName(const QString& provider_id) const {
  for (const QVariant& provider : available_providers_) {
    const QVariantMap values = provider.toMap();
    if (values.value(QStringLiteral("provider_id")).toString() == provider_id) {
      const QString displayName = values.value(QStringLiteral("display_name")).toString();
      return displayName.isEmpty() ? provider_id : displayName;
    }
  }
  return provider_id;
}

void ChatViewModel::syncAvailableOpenAiModels(const std::optional<ModelId>& preferredModelId) {
  openai_models_loaded_ = true;
  syncAvailableModels(preferredModelId);
}

void ChatViewModel::syncAvailableAnthropicModels(const std::optional<ModelId>& preferredModelId) {
  anthropic_models_loaded_ = true;
  syncAvailableModels(preferredModelId);
}

void ChatViewModel::syncAvailableGoogleModels(const std::optional<ModelId>& preferredModelId) {
  google_models_loaded_ = true;
  syncAvailableModels(preferredModelId);
}

void ChatViewModel::syncNewMessagesIntoModel() {
  const auto& messages = conversation_->messages();
  const auto model_count = synced_message_count_;

  // Bring the message that was newest at the last sync up to date in place -- it may have changed
  // (e.g. ChatController::handleToolCall() finalizes it to MessageStatus::Complete before
  // appending invocation/result rows below it) without any StreamEvent of its own.
  if (model_count > 0 && model_count <= messages.size()) {
    message_model_->updateNewestMessage(messages[model_count - 1]);
  }

  // Insert any messages ChatController appended since then, oldest first: each insertNewestMessage
  // call places its row at index 0, pushing the previous insert down, so iterating chronologically
  // reproduces the correct newest-first order (REQ-F-007(4)).
  for (std::size_t index = model_count; index < messages.size(); ++index) {
    message_model_->insertNewestMessage(messages[index]);
  }
  synced_message_count_ = messages.size();
}

void ChatViewModel::persistNewMessagesIntoRepository() {
  // Tool-calling turns append invocation/result/next-placeholder Messages with no dedicated
  // persistence call of their own (ChatController fires at most one StreamEvent per turn segment
  // -- see syncNewMessagesIntoModel()). INSERT any of those before the caller's settled-update/usage
  // pass, otherwise persistMessageSettled()'s UPDATE silently matches zero rows and a subsequent
  // usage INSERT violates messages(id)'s foreign key.
  const QString conversationId = conversation_->id().toString();
  const auto& messages = conversation_->messages();
  for (std::size_t index = persisted_message_count_; index < messages.size(); ++index) {
    repository_->persistNewMessage(conversationId, messages[index]);
  }
  persisted_message_count_ = messages.size();
}

void ChatViewModel::onStreamEvent(const StreamEvent& event) {
  syncNewMessagesIntoModel();
  if (const auto* streamError = std::get_if<holonight_domain::Error>(&event)) {
    const QString contextualMessage = tr("%1 / %2: %3")
                                          .arg(providerDisplayName(selected_model_id_.provider_id),
                                               selected_model_id_.model_name, streamError->message);
    setErrorMessage(contextualMessage);
    emit requestFailed(conversation_->title(), contextualMessage);
  } else if (std::holds_alternative<holonight_domain::Completed>(event)) {
    emit responseReady(conversation_->title());
  }

  if (persistence_enabled_) {
    persistNewMessagesIntoRepository();

    const holonight_domain::Message& lastMessage = conversation_->messages().back();
    const bool isTerminal = lastMessage.status() == holonight_domain::MessageStatus::Complete ||
                            lastMessage.status() == holonight_domain::MessageStatus::Error ||
                            lastMessage.status() == holonight_domain::MessageStatus::Cancelled;
    if (isTerminal) {
      repository_->persistMessageSettled(conversation_->id().toString(), lastMessage);

      if (lastMessage.role() == holonight_domain::MessageRole::Assistant) {
        const auto [usage, modelIdentifier] = std::visit(
            [](const auto& value) -> std::pair<std::optional<holonight_domain::Usage>, std::optional<QString>> {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, holonight_domain::Completed> ||
                            std::is_same_v<T, holonight_domain::Error> ||
                            std::is_same_v<T, holonight_domain::Cancelled>) {
                return {value.usage, value.model_identifier};
              } else {
                return {std::nullopt, std::nullopt};
              }
            },
            event);
        // No usage row without a known model identifier (REQ-F-010) -- e.g. an error that occurred
        // before any provider ever reported which model actually served the request.
        if (usage.has_value() && modelIdentifier.has_value()) {
          repository_->persistUsage(conversation_->id().toString(), lastMessage.id().toString(), *modelIdentifier,
                                    *usage);
        }
      }
    }

    if (persistence_enabled_ && utility_task_runner_ && conversation_->messages().size() == 2 &&
        lastMessage.role() == holonight_domain::MessageRole::Assistant &&
        lastMessage.status() == holonight_domain::MessageStatus::Complete &&
        current_title_source_ == holonight_domain::TitleSource::Fallback) {
      const holonight_domain::Message& firstUserMessage = conversation_->messages().front();
      utility_task_runner_->requestTitleGeneration(conversation_->id(), firstUserMessage.text(), lastMessage.text(),
                                                   selected_model_id_);
    }
  }

  refreshComputedProperties();
}

void ChatViewModel::refreshComputedProperties() {
  const ProviderStatus status = deriveSelectedProviderStatus();
  if (status != selected_provider_status_) {
    selected_provider_status_ = status;
    emit selectedProviderStatusChanged();
  }

  // Derived from the conversation's last message rather than ChatController::isStreaming(): the
  // controller invokes our stream callback *before* it removes the stream from its own in-flight
  // bookkeeping (see handleStreamEvent()'s Completed/Error branches), so isStreaming() still
  // reports true for one more tick when onStreamEvent() calls this. The message status the
  // controller writes to the conversation is already authoritative and current at this point.
  bool streamingNow = false;
  bool regenerateNow = false;
  if (conversation_) {
    const auto& conversationMessages = conversation_->messages();
    if (!conversationMessages.empty()) {
      const auto& lastMessage = conversationMessages.back();
      if (lastMessage.role() == holonight_domain::MessageRole::Assistant) {
        streamingNow = lastMessage.status() == holonight_domain::MessageStatus::Pending ||
                       lastMessage.status() == holonight_domain::MessageStatus::Streaming;
        regenerateNow = lastMessage.status() == holonight_domain::MessageStatus::Error;
      }
    }
  }

  if (streamingNow != cached_is_streaming_) {
    cached_is_streaming_ = streamingNow;
    emit isStreamingChanged();
  }

  const bool providerReady =
      !provider_runtime_coordinator_ || provider_runtime_coordinator_->isReady(selected_model_id_.provider_id);
  const bool selectedAvailable = available_model_names_.contains(selected_model_id_.model_name);
  const bool sendNow =
      conversation_ != nullptr && messages_ready_ && !streamingNow && selectedAvailable && providerReady;
  if (sendNow != cached_can_send_) {
    cached_can_send_ = sendNow;
    emit canSendChanged();
  }

  regenerateNow = regenerateNow && selectedAvailable && providerReady;
  if (regenerateNow != cached_can_regenerate_) {
    cached_can_regenerate_ = regenerateNow;
    emit canRegenerateChanged();
  }
}

void ChatViewModel::setErrorMessage(QString message) {
  if (error_message_ == message) {
    return;
  }
  error_message_ = std::move(message);
  emit errorMessageChanged();
}

void ChatViewModel::setProviderStatusMessage(QString message) {
  if (provider_status_message_ == message) {
    return;
  }
  provider_status_message_ = std::move(message);
  emit providerStatusMessageChanged();
}

void ChatViewModel::prepareSelectedProvider() {
  if (!provider_runtime_coordinator_ || selected_model_id_.provider_id.isEmpty()) {
    return;
  }
  setProviderStatusMessage(provider_runtime_coordinator_->statusMessage(selected_model_id_.provider_id));
  provider_runtime_coordinator_->prepare(selected_model_id_.provider_id);
}

void ChatViewModel::registerRuntimeProvider(const holonight_config::ProviderInstanceConfig& instance) {
  if (!provider_runtime_coordinator_ || !adapter_router_) {
    return;
  }
  provider_runtime_coordinator_->registerProvider(
      instance.id, instance.display_name, credentialPolicy(instance.type),
      ProviderRuntimeOperations{
          .set_credential = [router = adapter_router_.get(), instance_id = instance.id](
                                const QString& value) { static_cast<void>(router->setCredential(instance_id, value)); },
          .refresh = [router = adapter_router_.get(), instance_id = instance.id](
                         const auto& success,
                         const auto& error) { static_cast<void>(router->refresh(instance_id, success, error)); },
          .models = [router = adapter_router_.get(), instance_id = instance.id]() -> const std::vector<ModelId>& {
            static const std::vector<ModelId> empty;
            const auto* models = router->availableModels(instance_id);
            return models == nullptr ? empty : *models;
          },
          .persist_models =
              [router = adapter_router_.get(), instance_id = instance.id] {
                const auto* models = router->availableModels(instance_id);
                if (models == nullptr) {
                  return;
                }
                QStringList names;
                for (const auto& model : *models) {
                  names.append(model.model_name);
                }
                holonight_config::ConfigRepository config(holonight_config::resolveConfigFilePath());
                static_cast<void>(config.saveCachedModels(instance_id, names));
              }});
  provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
  if (instance.enabled) {
    provider_runtime_coordinator_->prepare(instance.id);
  }
}

void ChatViewModel::onProviderChanged(const QString& provider_id) {
  if (provider_id == selected_model_id_.provider_id) {
    setProviderStatusMessage(provider_runtime_coordinator_->statusMessage(provider_id));
  }
  if (provider_runtime_coordinator_->refreshState(provider_id) == ProviderRefreshState::Succeeded) {
    if (adapter_router_) {
      instance_models_loaded_.insert(provider_id, true);
    } else if (provider_id == QStringLiteral("ollama")) {
      ollama_models_loaded_ = true;
    } else if (provider_id == QStringLiteral("openai")) {
      openai_models_loaded_ = true;
    } else if (provider_id == QStringLiteral("anthropic")) {
      anthropic_models_loaded_ = true;
    } else if (provider_id == QStringLiteral("google")) {
      google_models_loaded_ = true;
    }
    syncAvailableModels();
  } else if (provider_id == selected_model_id_.provider_id) {
    refreshComputedProperties();
  }
}

void ChatViewModel::applySelection(ModelId selection) {
  const ModelId previous = selected_model_id_;
  if (!selection.provider_id.isEmpty() && !selection.model_name.isEmpty()) {
    remembered_models_.insert(selection.provider_id, selection.model_name);
  }
  selected_model_id_ = std::move(selection);
  const QStringList names = modelNames(selected_model_id_.provider_id);
  if (names != available_model_names_) {
    available_model_names_ = names;
    emit availableModelNamesChanged();
  }
  if (previous.provider_id != selected_model_id_.provider_id) {
    emit selectedProviderIdChanged();
  }
  if (previous.model_name != selected_model_id_.model_name) {
    emit selectedModelNameChanged();
  }
}

QStringList ChatViewModel::modelNames(const QString& provider_id) const {
  const std::vector<ModelId>* models = nullptr;
  if (adapter_router_) {
    const auto instance =
        std::ranges::find(provider_state_.instances, provider_id, &holonight_config::ProviderInstanceConfig::id);
    if (instance == provider_state_.instances.end() || !instance->enabled) {
      return {};
    }
    models = adapter_router_->availableModels(provider_id);
  } else if (provider_id == QStringLiteral("ollama")) {
    models = &ollama_provider_->availableModels();
  } else if (provider_id == QStringLiteral("openai")) {
    models = &openai_provider_->availableModels();
  } else if (provider_id == QStringLiteral("anthropic")) {
    models = &anthropic_provider_->availableModels();
  } else if (provider_id == QStringLiteral("google")) {
    models = &google_provider_->availableModels();
  }
  QStringList names;
  if (models != nullptr) {
    names.reserve(static_cast<qsizetype>(models->size()));
    for (const ModelId& model : *models) {
      names.append(model.model_name);
    }
  }
  return names;
}

QString ChatViewModel::defaultModelName(const QString& provider_id) const {
  if (adapter_router_) {
    return instance_default_models_.value(provider_id);
  }
  if (provider_id == QStringLiteral("ollama")) {
    return default_models_.ollama;
  }
  if (provider_id == QStringLiteral("openai")) {
    return default_models_.openai;
  }
  if (provider_id == QStringLiteral("anthropic")) {
    return default_models_.anthropic;
  }
  if (provider_id == QStringLiteral("google")) {
    return default_models_.google;
  }
  return {};
}

bool ChatViewModel::providerHasReported(const QString& provider_id) const {
  if (adapter_router_) {
    return instance_models_loaded_.value(provider_id, false);
  }
  if (provider_id == QStringLiteral("ollama")) {
    return ollama_models_loaded_;
  }
  if (provider_id == QStringLiteral("openai")) {
    return openai_models_loaded_;
  }
  if (provider_id == QStringLiteral("anthropic")) {
    return anthropic_models_loaded_;
  }
  if (provider_id == QStringLiteral("google")) {
    return google_models_loaded_;
  }
  return true;
}

ChatViewModel::ProviderStatus ChatViewModel::deriveSelectedProviderStatus() const {
  if (selected_model_id_.provider_id.isEmpty()) {
    return ProviderStatus::Idle;
  }
  if (!provider_runtime_coordinator_) {
    return providerHasReported(selected_model_id_.provider_id) ? ProviderStatus::Connected
                                                               : ProviderStatus::LoadingModels;
  }
  switch (provider_runtime_coordinator_->readiness(selected_model_id_.provider_id)) {
    case ProviderReadiness::Unresolved:
      return ProviderStatus::Checking;
    case ProviderReadiness::MissingCredential:
      return ProviderStatus::SetupRequired;
    case ProviderReadiness::Unavailable:
      return ProviderStatus::Unavailable;
    case ProviderReadiness::Ready:
      break;
  }
  switch (provider_runtime_coordinator_->refreshState(selected_model_id_.provider_id)) {
    case ProviderRefreshState::NotStarted:
    case ProviderRefreshState::InProgress:
      return ProviderStatus::LoadingModels;
    case ProviderRefreshState::Succeeded:
      return ProviderStatus::Connected;
    case ProviderRefreshState::Failed:
      return ProviderStatus::Error;
  }
  return ProviderStatus::Idle;
}

void ChatViewModel::setPersistenceStatusMessage(QString message) {
  if (persistence_status_message_ == message) {
    return;
  }
  persistence_status_message_ = std::move(message);
  emit persistenceStatusMessageChanged();
}

void ChatViewModel::setMessagesReady(bool ready) {
  if (messages_ready_ == ready) {
    return;
  }
  messages_ready_ = ready;
  emit messagesReadyChanged();
  refreshComputedProperties();
}

void ChatViewModel::onRepositoryInitialized() {
  persistence_enabled_ = true;
  repository_->listConversations();
}

void ChatViewModel::onRepositoryUnavailable(const QString& reason) {
  persistence_enabled_ = false;
  setPersistenceStatusMessage(reason);

  if (!conversation_) {
    adoptTransientDraft();
  }
}

void ChatViewModel::onConversationListLoaded(const QList<ConversationSummary>& conversations) {
  conversation_list_model_->setAll(conversations);
  if (!conversations.isEmpty()) {
    switchConversation(conversations.front().id);
  } else {
    createConversation();
  }
}

void ChatViewModel::onConversationCreated(const ConversationSummary& summary) {
  conversation_list_model_->upsertToFront(summary);
  if (summary.id == active_conversation_id_) {
    return;
  }
  adoptConversation(summary, {});
}

void ChatViewModel::onConversationLoaded(const LoadedConversation& loaded) {
  adoptConversation(loaded.summary, loaded.messages);
  repository_->usageForConversation(loaded.summary.id);
}

void ChatViewModel::onConversationRenamed(const ConversationSummary& summary) {
  if (summary.id == active_conversation_id_ && conversation_) {
    conversation_->setTitle(summary.title);
    current_title_source_ = summary.title_source;
  }
  conversation_list_model_->upsertToFront(summary);
}

void ChatViewModel::onConversationDeleted(const QString& conversationId) {
  const bool wasActive = (conversationId == active_conversation_id_);
  conversation_list_model_->removeById(conversationId);
  if (!wasActive) {
    return;
  }
  if (const auto first = conversation_list_model_->firstConversationId(); first.has_value()) {
    switchConversation(*first);
  } else {
    createConversation();
  }
}

void ChatViewModel::onConversationPinned(const QString& conversationId, const QDateTime& pinnedAt) {
  conversation_list_model_->markPinned(conversationId, pinnedAt);
  emit conversationPinned(QUuid::fromString(conversationId));
}

void ChatViewModel::onConversationUnpinned(const QString& conversationId) {
  conversation_list_model_->markUnpinned(conversationId);
  emit conversationUnpinned(QUuid::fromString(conversationId));
}

void ChatViewModel::onLastModelIdUpdated(const QString& conversationId, const ModelId& modelId,
                                         const QDateTime& updatedAt) {
  Q_UNUSED(modelId)
  conversation_list_model_->touchToFront(conversationId, updatedAt);
}

void ChatViewModel::onRepositoryError(const QString& conversationId, const QString& message) {
  Q_UNUSED(conversationId)
  setMessagesReady(conversation_ != nullptr);
  setErrorMessage(message);
}

void ChatViewModel::onTitleGenerated(const QString& conversationId, const QString& title) {
  if (!persistence_enabled_) {
    return;
  }
  repository_->renameConversation(conversationId, title, holonight_domain::TitleSource::Generated);
}

void ChatViewModel::onTitleGenerationStarted(const QString& conversationId) {
  conversation_list_model_->setTitleGenerationInProgress(conversationId, true);
}

void ChatViewModel::onTitleGenerationFinished(const QString& conversationId) {
  conversation_list_model_->setTitleGenerationInProgress(conversationId, false);
}

void ChatViewModel::onUsageForConversationLoaded(
    const QString& conversationId, const QMap<QString, holonight_persistence::UsageRecord>& usageByMessageId) {
  if (conversationId != active_conversation_id_) {
    return;
  }
  message_model_->applyUsageRecords(usageByMessageId);
}

void ChatViewModel::onUsagePersisted(const QString& conversationId, const QString& messageId,
                                     const holonight_persistence::UsageRecord& record) {
  if (conversationId != active_conversation_id_) {
    return;
  }
  message_model_->applyUsageRecords({{messageId, record}});
}

void ChatViewModel::adoptConversation(const ConversationSummary& summary,
                                      std::vector<holonight_domain::Message> messages) {
  conversation_materialized_ = true;
  const ModelId previousSelection = selected_model_id_;
  auto newConversation =
      std::make_shared<Conversation>(ConversationId::fromString(summary.id), summary.title, summary.created_at);
  for (holonight_domain::Message& message : messages) {
    newConversation->appendMessage(std::move(message));
  }
  conversation_ = std::move(newConversation);
  persisted_message_count_ = conversation_->messages().size();
  message_model_->resetFromChronological(conversation_->messages());
  synced_message_count_ = conversation_->messages().size();
  setMessagesReady(true);

  if (summary.last_model_id.has_value()) {
    const ModelId& restored = *summary.last_model_id;
    if (modelNames(restored.provider_id).contains(restored.model_name)) {
      applySelection(restored);
    } else if (adapter_router_ || providerHasReported(restored.provider_id)) {
      applySelection(selectReplacementModel(restored, available_providers_));
    } else {
      applySelection(restored);
    }
  } else if (!previousSelection.provider_id.isEmpty()) {
    applySelection(previousSelection);
  } else {
    if (!available_providers_.isEmpty()) {
      const QString providerId = available_providers_.front().toMap().value(QStringLiteral("provider_id")).toString();
      const QStringList names = modelNames(providerId);
      const QString defaultName = defaultModelName(providerId);
      applySelection(
          ModelId{.provider_id = providerId, .model_name = names.contains(defaultName) ? defaultName : names.front()});
    }
  }

  active_conversation_id_ = summary.id;
  current_title_source_ = summary.title_source;
  emit activeConversationIdChanged();

  setErrorMessage(QString());
  prepareSelectedProvider();
  refreshComputedProperties();
}

void ChatViewModel::adoptTransientDraft() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  adoptConversation(
      ConversationSummary{
          .id = ConversationId::generate().toString(),
          .title = holonight_persistence::kDefaultConversationTitle,
          .created_at = now,
          .updated_at = now,
          .last_model_id = std::nullopt,
      },
      {});
  conversation_materialized_ = false;
}

}  // namespace holonight_application
