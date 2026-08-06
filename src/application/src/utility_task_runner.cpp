#include "holonight_application/utility_task_runner.h"

#include "holonight_providers/qt_network_http_client.h"

#include <QDebug>
#include <QString>

#include <algorithm>
#include <array>
#include <ranges>
#include <type_traits>
#include <variant>

namespace holonight_application {

namespace {

constexpr double kUtilityTemperature = 0.3;
constexpr int kAnthropicUtilityMaxOutputTokens = 64;
constexpr int kGoogleUtilityMaxOutputTokens = 1024;

// REQ-F-016/REQ-NF-001: injects utility-specific generation parameters into a COPY of an instance's
// settings before it reaches the utility router, so the chat window's configured
// temperature/max-tokens for the same provider instance remain unaffected.
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

std::shared_ptr<holonight_providers::HttpClient> clientFor(holonight_config::ProviderType type,
                                                           const UtilityProviderEndpoints& endpoints) {
  using enum holonight_config::ProviderType;
  switch (type) {
    case Ollama:
      return endpoints.ollama_http_client ? endpoints.ollama_http_client
                                          : std::make_shared<holonight_providers::QtNetworkHttpClient>();
    case OpenAi:
      return endpoints.openai_http_client ? endpoints.openai_http_client
                                          : std::make_shared<holonight_providers::QtNetworkHttpClient>();
    case Anthropic:
      return endpoints.anthropic_http_client ? endpoints.anthropic_http_client
                                             : std::make_shared<holonight_providers::QtNetworkHttpClient>();
    case Google:
      return endpoints.google_http_client ? endpoints.google_http_client
                                          : std::make_shared<holonight_providers::QtNetworkHttpClient>();
  }
  return {};
}

}  // namespace

using holonight_domain::Cancelled;
using holonight_domain::Completed;
using holonight_domain::ContentDelta;
using holonight_domain::Error;
using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::StreamEvent;
using holonight_providers::AnthropicProvider;
using holonight_providers::GoogleProvider;
using holonight_providers::OllamaProvider;
using holonight_providers::OpenAIProvider;
using holonight_providers::QtNetworkHttpClient;

UtilityTaskRunner::UtilityTaskRunner(holonight_credentials::CredentialStore* credential_store,
                                     holonight_config::UtilityConfig utility_config,
                                     const UtilityProviderEndpoints& endpoints, QObject* parent,
                                     holonight_config::ProviderState provider_state,
                                     const QHash<QString, std::vector<holonight_domain::ModelId>>& available_models)
    : QObject(parent),
      utility_config_(std::move(utility_config)),
      provider_state_(std::move(provider_state)),
      endpoints_(endpoints) {
  provider_runtime_coordinator_ = std::make_unique<ProviderRuntimeCoordinator>(credential_store);
  connect(provider_runtime_coordinator_.get(), &ProviderRuntimeCoordinator::providerChanged, this,
          &UtilityTaskRunner::onProviderChanged);

  if (!provider_state_.instances.empty()) {
    adapter_router_ = std::make_unique<ProviderAdapterRouter>();
    for (const auto& instance : provider_state_.instances) {
      if (!instance.enabled ||
          !adapter_router_->add(withUtilityGenerationParams(instance), clientFor(instance.type, endpoints_))) {
        continue;
      }
      static_cast<void>(adapter_router_->restoreAvailableModels(instance.id, available_models.value(instance.id)));
      registerRuntimeProvider(instance);
    }
    return;
  }

  ollama_provider_ = std::make_shared<OllamaProvider>(
      endpoints.ollama_http_client ? endpoints.ollama_http_client : std::make_shared<QtNetworkHttpClient>(),
      endpoints.ollama_base_url);
  openai_provider_ = std::make_shared<OpenAIProvider>(
      endpoints.openai_http_client ? endpoints.openai_http_client : std::make_shared<QtNetworkHttpClient>(),
      endpoints.openai_base_url);
  anthropic_provider_ = std::make_shared<AnthropicProvider>(
      endpoints.anthropic_http_client ? endpoints.anthropic_http_client : std::make_shared<QtNetworkHttpClient>(),
      endpoints.anthropic_base_url);
  google_provider_ = std::make_shared<GoogleProvider>(
      endpoints.google_http_client ? endpoints.google_http_client : std::make_shared<QtNetworkHttpClient>(),
      endpoints.google_base_url);

  // Fixed, deliberately conservative — title generation wants short, deterministic output,
  // independent of whatever temperature/token-budget the user has dialed in for live chat
  // (REQ-NF-001's own rationale: stateful setters must not cross-contaminate).
  ollama_provider_->setTemperature(0.3);
  openai_provider_->setTemperature(0.3);
  anthropic_provider_->setTemperature(0.3);
  anthropic_provider_->setMaxOutputTokens(64);
  google_provider_->setTemperature(0.3);
  google_provider_->setMaxOutputTokens(kGoogleUtilityMaxOutputTokens);

  // registerProvider() bound to THIS instance's providers — a second, independent registration
  // under the same provider_id strings ("ollama", "openai", ...) in a SEPARATE coordinator
  // instance from ChatViewModel's (see DESIGN.md §5/§11 Alternative 3).
  provider_runtime_coordinator_->registerProvider(
      QStringLiteral("ollama"), QStringLiteral("Ollama"), CredentialPolicy::Optional,
      ProviderRuntimeOperations{
          .set_credential = [provider = ollama_provider_](const QString& value) { provider->setAuthToken(value); },
          .refresh = [provider = ollama_provider_](const auto& success,
                                                   const auto& error) { provider->refresh(success, error); },
          .models = [provider = ollama_provider_]() -> const std::vector<holonight_domain::ModelId>& {
            return provider->availableModels();
          },
          // Deliberate no-op: model-list caching to config.json stays ChatViewModel's exclusive
          // responsibility (its own coordinator/provider instances already own that write).
          .persist_models = [] {}});
  provider_runtime_coordinator_->registerProvider(
      QStringLiteral("openai"), QStringLiteral("OpenAI"), CredentialPolicy::Required,
      ProviderRuntimeOperations{
          .set_credential = [provider = openai_provider_](const QString& value) { provider->setAuthToken(value); },
          .refresh = [provider = openai_provider_](const auto& success,
                                                   const auto& error) { provider->refresh(success, error); },
          .models = [provider = openai_provider_]() -> const std::vector<holonight_domain::ModelId>& {
            return provider->availableModels();
          },
          .persist_models = [] {}});
  provider_runtime_coordinator_->registerProvider(
      QStringLiteral("anthropic"), QStringLiteral("Anthropic"), CredentialPolicy::Required,
      ProviderRuntimeOperations{
          .set_credential = [provider = anthropic_provider_](const QString& value) { provider->setAuthKey(value); },
          .refresh = [provider = anthropic_provider_](const auto& success,
                                                      const auto& error) { provider->refresh(success, error); },
          .models = [provider = anthropic_provider_]() -> const std::vector<holonight_domain::ModelId>& {
            return provider->availableModels();
          },
          .persist_models = [] {}});
  provider_runtime_coordinator_->registerProvider(
      QStringLiteral("google"), QStringLiteral("Google"), CredentialPolicy::Required,
      ProviderRuntimeOperations{
          .set_credential = [provider = google_provider_](const QString& value) { provider->setAuthKey(value); },
          .refresh = [provider = google_provider_](const auto& success,
                                                   const auto& error) { provider->refresh(success, error); },
          .models = [provider = google_provider_]() -> const std::vector<holonight_domain::ModelId>& {
            return provider->availableModels();
          },
          .persist_models = [] {}});

  for (const QString& providerId :
       {QStringLiteral("ollama"), QStringLiteral("openai"), QStringLiteral("anthropic"), QStringLiteral("google")}) {
    provider_runtime_coordinator_->prepare(providerId);
  }
}

void UtilityTaskRunner::requestTitleGeneration(const holonight_domain::ConversationId& conversationId,
                                               const QString& firstUserText, const QString& firstAssistantText,
                                               const holonight_domain::ModelId& chatFallbackModel,
                                               const std::optional<holonight_domain::ModelId>& taskOverride) {
  if (!utility_config_.chat_title_generation_enabled.value_or(true)) {
    return;  // REQ-F-009: disabled — no dispatch, no generations_ entry, no log noise
  }

  const QString conversationKey = conversationId.toString();
  if (generations_.contains(conversationKey)) {
    return;  // already in flight (REQ-NF-004/005) — silent no-op, no retry
  }

  const std::optional<holonight_domain::ModelId> model = resolveModel(std::nullopt, chatFallbackModel);
  if (!model.has_value()) {
    generations_.insert(conversationKey, InFlightGeneration{.settled = true});
    return;
  }

  generations_.insert(conversationKey, InFlightGeneration{
                                           .model = *model,
                                           .first_user_text = firstUserText,
                                           .first_assistant_text = firstAssistantText,
                                       });
  emit titleGenerationStarted(conversationKey);
  continuePendingGeneration(conversationKey);
}

std::optional<holonight_domain::ModelId> UtilityTaskRunner::resolveModel(
    const std::optional<holonight_domain::ModelId>& taskOverride,
    const holonight_domain::ModelId& chatFallbackModel) const {
  if (!adapter_router_) {
    return resolveModelLegacy(taskOverride, chatFallbackModel);
  }
  return resolveModelViaRouter(taskOverride, chatFallbackModel);
}

std::optional<holonight_domain::ModelId> UtilityTaskRunner::resolveModelLegacy(
    const std::optional<holonight_domain::ModelId>& taskOverride,
    const holonight_domain::ModelId& chatFallbackModel) const {
  static const std::array<QString, 4> knownProviderIds{QStringLiteral("ollama"), QStringLiteral("openai"),
                                                       QStringLiteral("anthropic"), QStringLiteral("google")};
  const auto isKnownProvider = [](const holonight_domain::ModelId& model) {
    return std::ranges::find(knownProviderIds, model.provider_id) != knownProviderIds.end();
  };

  if (taskOverride.has_value() && isKnownProvider(*taskOverride)) {
    return *taskOverride;
  }
  if (utility_config_.chat_title_model_override.has_value() &&
      isKnownProvider(*utility_config_.chat_title_model_override)) {
    return utility_config_.chat_title_model_override;
  }
  if (!utility_config_.default_utility_model.has_value()) {
    return chatFallbackModel;
  }
  if (!isKnownProvider(*utility_config_.default_utility_model)) {
    qWarning().noquote() << "holonight_application: UtilityTaskRunner: default_utility_model has unknown "
                            "provider_id"
                         << utility_config_.default_utility_model->provider_id << "— skipping title generation";
    return std::nullopt;
  }
  return utility_config_.default_utility_model;
}

std::optional<holonight_domain::ModelId> UtilityTaskRunner::resolveModelViaRouter(
    const std::optional<holonight_domain::ModelId>& taskOverride,
    const holonight_domain::ModelId& chatFallbackModel) const {
  const auto isUsable = [this](const holonight_domain::ModelId& model) {
    if (!adapter_router_->isEnabled(model.provider_id)) {
      return false;
    }
    const ProviderReadiness readiness = provider_runtime_coordinator_->readiness(model.provider_id);
    if (readiness != ProviderReadiness::Ready && readiness != ProviderReadiness::Unresolved) {
      return false;
    }
    const auto* models = adapter_router_->availableModels(model.provider_id);
    return models != nullptr && std::ranges::find(*models, model) != models->end();
  };
  if (taskOverride.has_value() && isUsable(*taskOverride)) {
    return *taskOverride;
  }
  if (utility_config_.chat_title_model_override.has_value() && isUsable(*utility_config_.chat_title_model_override)) {
    return utility_config_.chat_title_model_override;
  }
  if (utility_config_.default_utility_model.has_value() && isUsable(*utility_config_.default_utility_model)) {
    return utility_config_.default_utility_model;
  }
  if (isUsable(chatFallbackModel)) {
    return chatFallbackModel;
  }
  for (const auto& instance : provider_state_.instances) {
    const auto* models = adapter_router_->availableModels(instance.id);
    if (instance.enabled && models != nullptr && !models->empty() && isUsable(models->front())) {
      return models->front();
    }
  }
  return std::nullopt;
}

void UtilityTaskRunner::continuePendingGeneration(const QString& conversationKey) {
  auto entry = generations_.find(conversationKey);
  if (entry == generations_.end() || entry->settled || entry->handle) {
    return;
  }

  const ProviderReadiness readiness = provider_runtime_coordinator_->readiness(entry->model.provider_id);
  if (readiness == ProviderReadiness::Unresolved) {
    return;
  }
  if (readiness != ProviderReadiness::Ready) {
    qWarning().noquote() << "holonight_application: UtilityTaskRunner: provider" << entry->model.provider_id
                         << "not ready for title generation, conversation" << conversationKey;
    settleGeneration(conversationKey);
    return;
  }

  dispatchGeneration(conversationKey, entry->model, entry->first_user_text, entry->first_assistant_text);
}

void UtilityTaskRunner::settleGeneration(const QString& conversationKey) {
  auto entry = generations_.find(conversationKey);
  if (entry == generations_.end() || entry->settled) {
    return;
  }
  entry->settled = true;
  entry->first_user_text.clear();
  entry->first_assistant_text.clear();
  entry->accumulated_text.clear();
  entry->handle.reset();
  emit titleGenerationFinished(conversationKey);
}

void UtilityTaskRunner::onProviderChanged(const QString& providerId) {
  const QList<QString> conversationKeys = generations_.keys();
  for (const QString& conversationKey : conversationKeys) {
    const auto entry = generations_.constFind(conversationKey);
    if (entry != generations_.cend() && !entry->settled && !entry->handle && entry->model.provider_id == providerId) {
      continuePendingGeneration(conversationKey);
    }
  }
}

void UtilityTaskRunner::dispatchGeneration(const QString& conversationKey, const holonight_domain::ModelId& model,
                                           const QString& firstUserText, const QString& firstAssistantText) {
  const QString prompt = QStringLiteral(
                             "Write a short, descriptive title (max 6 words, no quotes, no trailing punctuation) "
                             "for this exchange.\n\nUser: %1\n\nAssistant: %2")
                             .arg(firstUserText, firstAssistantText);
  const std::vector<Message> history{Message(MessageId::generate(), MessageRole::User, prompt)};

  InFlightGeneration& entry = generations_[conversationKey];
  entry.handle = dispatchSendChat(model, history, [this, conversationKey](const StreamEvent& event) {
    auto entryIt = generations_.find(conversationKey);
    if (entryIt == generations_.end() || entryIt->settled) {
      return;
    }
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, ContentDelta>) {
            entryIt->accumulated_text += value.text;
          } else if constexpr (std::is_same_v<T, Completed>) {
            const QString title = entryIt->accumulated_text.trimmed();
            if (!title.isEmpty()) {
              emit titleGenerated(conversationKey, title);
            } else {
              qWarning().noquote() << "holonight_application: UtilityTaskRunner: empty title for" << conversationKey
                                   << "- keeping fallback (REQ-F-008).";
            }
            settleGeneration(conversationKey);
          } else if constexpr (std::is_same_v<T, Error>) {
            qWarning().noquote() << "holonight_application: UtilityTaskRunner: generation failed for" << conversationKey
                                 << ":" << value.message;
            settleGeneration(conversationKey);
          } else if constexpr (std::is_same_v<T, Cancelled>) {
            settleGeneration(conversationKey);  // not expected to occur (nothing calls cancel())
          }
        },
        event);
  });
}

holonight_providers::HttpRequestHandlePtr UtilityTaskRunner::dispatchSendChat(
    const holonight_domain::ModelId& model, const std::vector<Message>& history,
    const std::function<void(const StreamEvent&)>& onEvent) {
  if (adapter_router_) {
    return adapter_router_->sendChat(model, history, onEvent);
  }
  if (model.provider_id == QStringLiteral("openai")) {
    return openai_provider_->sendChat(model, history, onEvent);
  }
  if (model.provider_id == QStringLiteral("anthropic")) {
    return anthropic_provider_->sendChat(model, history, onEvent);
  }
  if (model.provider_id == QStringLiteral("google")) {
    return google_provider_->sendChat(model, history, onEvent);
  }
  return ollama_provider_->sendChat(model, history, onEvent);
}

void UtilityTaskRunner::registerRuntimeProvider(const holonight_config::ProviderInstanceConfig& instance) {
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
          .models = [router = adapter_router_.get(),
                     instance_id = instance.id]() -> const std::vector<holonight_domain::ModelId>& {
            static const std::vector<holonight_domain::ModelId> empty;
            const auto* models = router->availableModels(instance_id);
            return models == nullptr ? empty : *models;
          },
          // Deliberate no-op: model-list caching to config.json stays ChatViewModel's exclusive
          // responsibility (its own coordinator/provider instances already own that write).
          .persist_models = [] {}});
  provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
  if (instance.enabled) {
    provider_runtime_coordinator_->prepare(instance.id);
  }
}

void UtilityTaskRunner::applyProviderState(holonight_config::ProviderState provider_state) {
  if (!adapter_router_) {
    provider_state_ = std::move(provider_state);  // legacy fallback path: nothing to resync
    return;
  }

  for (const auto& previous : provider_state_.instances) {
    if (std::ranges::find(provider_state.instances, previous.id, &holonight_config::ProviderInstanceConfig::id) ==
        provider_state.instances.end()) {
      static_cast<void>(provider_runtime_coordinator_->unregisterProvider(previous.id));
      static_cast<void>(adapter_router_->remove(previous.id));  // REQ-F-017: no-op if streaming
    }
  }

  for (const auto& instance : provider_state.instances) {
    const auto utilityInstance = withUtilityGenerationParams(instance);
    if (!adapter_router_->contains(instance.id)) {
      if (adapter_router_->add(utilityInstance, clientFor(instance.type, endpoints_))) {
        registerRuntimeProvider(instance);
      }
    } else {
      static_cast<void>(adapter_router_->reconfigure(utilityInstance));
      static_cast<void>(adapter_router_->setEnabled(instance.id, instance.enabled));
      provider_runtime_coordinator_->setEnabled(instance.id, instance.enabled);
    }
  }

  provider_state_ = std::move(provider_state);
}

void UtilityTaskRunner::applyUtilityConfig(holonight_config::UtilityConfig utility_config) {
  utility_config_ = std::move(utility_config);
}

}  // namespace holonight_application
