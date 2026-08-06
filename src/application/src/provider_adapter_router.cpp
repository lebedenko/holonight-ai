#include "holonight_application/provider_adapter_router.h"

#include <type_traits>
#include <utility>

namespace holonight_application {
namespace {

template <typename Provider, typename Settings>
void applySettings(const std::shared_ptr<Provider>& provider, const Settings& settings);

template <>
void applySettings(const std::shared_ptr<holonight_providers::OllamaProvider>& provider,
                   const holonight_config::OllamaProviderConfig& settings) {
  provider->setBaseUrl(settings.base_url);
  provider->setContextWindow(settings.context_window);
  provider->setTemperature(settings.temperature);
}

template <>
void applySettings(const std::shared_ptr<holonight_providers::OpenAIProvider>& provider,
                   const holonight_config::OpenAIProviderConfig& settings) {
  provider->setBaseUrl(settings.base_url);
  provider->setTemperature(settings.temperature);
}

template <>
void applySettings(const std::shared_ptr<holonight_providers::AnthropicProvider>& provider,
                   const holonight_config::AnthropicProviderConfig& settings) {
  provider->setBaseUrl(settings.base_url);
  provider->setTemperature(settings.temperature);
  provider->setMaxOutputTokens(settings.max_output_tokens);
}

template <>
void applySettings(const std::shared_ptr<holonight_providers::GoogleProvider>& provider,
                   const holonight_config::GoogleProviderConfig& settings) {
  provider->setBaseUrl(settings.base_url);
  provider->setTemperature(settings.temperature);
  provider->setMaxOutputTokens(settings.max_output_tokens);
}

template <typename Provider, typename Settings>
std::optional<ProviderAdapter> create(const holonight_config::ProviderInstanceConfig& config,
                                      const std::shared_ptr<holonight_providers::HttpClient>& httpClient) {
  const auto* settings = std::get_if<Settings>(&config.settings);
  if (settings == nullptr || config.id.isEmpty() || httpClient == nullptr) {
    return std::nullopt;
  }

  auto provider = std::make_shared<Provider>(httpClient, settings->base_url, config.id);
  applySettings(provider, *settings);
  return ProviderAdapter{std::move(provider)};
}

}  // namespace

std::optional<ProviderAdapter> createProviderAdapter(
    const holonight_config::ProviderInstanceConfig& config,
    const std::shared_ptr<holonight_providers::HttpClient>& httpClient) {
  using enum holonight_config::ProviderType;
  switch (config.type) {
    case Ollama:
      return create<holonight_providers::OllamaProvider, holonight_config::OllamaProviderConfig>(config, httpClient);
    case OpenAi:
      return create<holonight_providers::OpenAIProvider, holonight_config::OpenAIProviderConfig>(config, httpClient);
    case Anthropic:
      return create<holonight_providers::AnthropicProvider, holonight_config::AnthropicProviderConfig>(config,
                                                                                                       httpClient);
    case Google:
      return create<holonight_providers::GoogleProvider, holonight_config::GoogleProviderConfig>(config, httpClient);
  }
  return std::nullopt;
}

bool ProviderAdapterRouter::add(const holonight_config::ProviderInstanceConfig& config,
                                const std::shared_ptr<holonight_providers::HttpClient>& httpClient) {
  if (records_.contains(config.id)) {
    return false;
  }
  auto adapter = createProviderAdapter(config, httpClient);
  if (!adapter.has_value()) {
    return false;
  }
  records_.insert(config.id,
                  RuntimeRecord{.adapter = std::move(*adapter), .config = config, .enabled = config.enabled});
  return true;
}

bool ProviderAdapterRouter::remove(const QString& instanceId) {
  const auto found = records_.constFind(instanceId);
  if (found == records_.cend() || found->active_stream_count != 0) {
    return false;
  }
  return static_cast<int>(records_.remove(instanceId)) > 0;
}

bool ProviderAdapterRouter::contains(const QString& instanceId) const { return records_.contains(instanceId); }

bool ProviderAdapterRouter::reconfigure(const holonight_config::ProviderInstanceConfig& config) {
  const auto record_iterator = records_.find(config.id);
  if (record_iterator == records_.end()) {
    return false;
  }

  return std::visit(
      [&config, &record_iterator](const auto& provider) {
        using Provider = std::decay_t<decltype(provider)>::element_type;
        using Settings = std::conditional_t<
            std::is_same_v<Provider, holonight_providers::OllamaProvider>, holonight_config::OllamaProviderConfig,
            std::conditional_t<
                std::is_same_v<Provider, holonight_providers::OpenAIProvider>, holonight_config::OpenAIProviderConfig,
                std::conditional_t<std::is_same_v<Provider, holonight_providers::AnthropicProvider>,
                                   holonight_config::AnthropicProviderConfig, holonight_config::GoogleProviderConfig>>>;
        constexpr holonight_config::ProviderType providerType = [] {
          if constexpr (std::is_same_v<Provider, holonight_providers::OllamaProvider>) {
            return holonight_config::ProviderType::Ollama;
          } else if constexpr (std::is_same_v<Provider, holonight_providers::OpenAIProvider>) {
            return holonight_config::ProviderType::OpenAi;
          } else if constexpr (std::is_same_v<Provider, holonight_providers::AnthropicProvider>) {
            return holonight_config::ProviderType::Anthropic;
          } else {
            return holonight_config::ProviderType::Google;
          }
        }();
        const auto* settings = std::get_if<Settings>(&config.settings);
        if (config.type != providerType || settings == nullptr) {
          return false;
        }
        applySettings(provider, *settings);
        record_iterator->config = config;
        return true;
      },
      record_iterator->adapter);
}

bool ProviderAdapterRouter::setEnabled(const QString& instanceId, bool enabled) {
  auto found = records_.find(instanceId);
  if (found == records_.end()) {
    return false;
  }
  found->enabled = enabled;
  return true;
}

bool ProviderAdapterRouter::setCredential(const QString& instanceId, const QString& credential) {
  auto found = records_.find(instanceId);
  if (found == records_.end()) {
    return false;
  }
  std::visit(
      [&credential](const auto& provider) {
        using Provider = std::decay_t<decltype(provider)>::element_type;
        if constexpr (std::is_same_v<Provider, holonight_providers::OllamaProvider> ||
                      std::is_same_v<Provider, holonight_providers::OpenAIProvider>) {
          provider->setAuthToken(credential);
        } else {
          provider->setAuthKey(credential);
        }
      },
      found->adapter);
  return true;
}

bool ProviderAdapterRouter::isEnabled(const QString& instanceId) const {
  const auto found = records_.constFind(instanceId);
  return found != records_.cend() && found->enabled;
}

qsizetype ProviderAdapterRouter::activeStreamCount(const QString& instanceId) const {
  const auto found = records_.constFind(instanceId);
  return found == records_.cend() ? 0 : found->active_stream_count;
}

bool ProviderAdapterRouter::canDelete(const QString& instanceId) const {
  const auto found = records_.constFind(instanceId);
  return found != records_.cend() && found->active_stream_count == 0;
}

const std::vector<holonight_domain::ModelId>* ProviderAdapterRouter::availableModels(const QString& instanceId) const {
  const auto record_iterator = records_.constFind(instanceId);
  if (record_iterator == records_.cend()) {
    return nullptr;
  }
  return std::visit([](const auto& provider) { return &provider->availableModels(); }, record_iterator->adapter);
}

bool ProviderAdapterRouter::restoreAvailableModels(const QString& instanceId,
                                                   std::vector<holonight_domain::ModelId> models) {
  auto record_iterator = records_.find(instanceId);
  if (record_iterator == records_.end()) {
    return false;
  }
  std::erase_if(models, [&instanceId](const auto& model) { return model.provider_id != instanceId; });
  std::visit([&models](const auto& provider) { provider->restoreAvailableModels(std::move(models)); },
             record_iterator->adapter);
  return true;
}

bool ProviderAdapterRouter::refresh(const QString& instanceId, const std::function<void()>& onSuccess,
                                    const std::function<void(const QString&)>& onError) {
  const auto record_iterator = records_.find(instanceId);
  if (record_iterator == records_.end() || !record_iterator->enabled) {
    return false;
  }
  std::visit([&](const auto& provider) { provider->refresh(onSuccess, onError); }, record_iterator->adapter);
  return true;
}

holonight_providers::HttpRequestHandlePtr ProviderAdapterRouter::sendChat(
    const holonight_domain::ModelId& model, const std::vector<holonight_domain::Message>& history,
    const std::function<void(const holonight_domain::StreamEvent&)>& onEvent, std::chrono::milliseconds idleTimeout) {
  auto record_iterator = records_.find(model.provider_id);
  if (record_iterator == records_.end() || !record_iterator->enabled) {
    return {};
  }
  ++record_iterator->active_stream_count;
  emit activeStreamCountChanged(model.provider_id);
  const QString instanceId = model.provider_id;
  auto terminalSeen = std::make_shared<bool>(false);

  // Only explicitly enabled adapters receive the provider-neutral local catalog.
  holonight_domain::ToolCatalogSnapshot toolCatalog;
  if (tool_registry_ != nullptr) {
    if (const auto* openAiConfig =
            std::get_if<holonight_config::OpenAIProviderConfig>(&record_iterator->config.settings);
        openAiConfig != nullptr && openAiConfig->tool_calling_enabled) {
      toolCatalog = tool_registry_->catalogSnapshot();
    } else if (const auto* anthropicConfig =
                   std::get_if<holonight_config::AnthropicProviderConfig>(&record_iterator->config.settings);
               anthropicConfig != nullptr && anthropicConfig->tool_calling_enabled) {
      toolCatalog = tool_registry_->catalogSnapshot();
    } else if (const auto* googleConfig =
                   std::get_if<holonight_config::GoogleProviderConfig>(&record_iterator->config.settings);
               googleConfig != nullptr && googleConfig->tool_calling_enabled) {
      toolCatalog = tool_registry_->catalogSnapshot();
    } else if (const auto* ollamaConfig =
                   std::get_if<holonight_config::OllamaProviderConfig>(&record_iterator->config.settings);
               ollamaConfig != nullptr && ollamaConfig->tool_calling_enabled) {
      toolCatalog = tool_registry_->catalogSnapshot();
    }
  }

  auto request = std::visit(
      [&](const auto& provider) {
        using Provider = std::decay_t<decltype(provider)>::element_type;
        auto handler = [this, instanceId, onEvent, terminalSeen](const holonight_domain::StreamEvent& event) {
          const bool isTerminal = std::holds_alternative<holonight_domain::Completed>(event) ||
                                  std::holds_alternative<holonight_domain::Error>(event) ||
                                  std::holds_alternative<holonight_domain::Cancelled>(event);
          if (!*terminalSeen && isTerminal) {
            *terminalSeen = true;
            auto current = records_.find(instanceId);
            if (current != records_.end() && current->active_stream_count > 0) {
              --current->active_stream_count;
              emit activeStreamCountChanged(instanceId);
            }
          }
          if (onEvent) {
            onEvent(event);
          }
        };
        if constexpr (std::is_same_v<Provider, holonight_providers::OpenAIProvider> ||
                      std::is_same_v<Provider, holonight_providers::AnthropicProvider> ||
                      std::is_same_v<Provider, holonight_providers::GoogleProvider> ||
                      std::is_same_v<Provider, holonight_providers::OllamaProvider>) {
          return provider->sendChat(model, history, handler, idleTimeout, toolCatalog);
        } else {
          return provider->sendChat(model, history, handler, idleTimeout);
        }
      },
      record_iterator->adapter);
  if (request == nullptr && !*terminalSeen) {
    --record_iterator->active_stream_count;
    emit activeStreamCountChanged(instanceId);
  }
  return request;
}

void ProviderAdapterRouter::cancel(const holonight_providers::HttpRequestHandlePtr& request) {
  if (request != nullptr) {
    request->cancel();
  }
}

}  // namespace holonight_application
