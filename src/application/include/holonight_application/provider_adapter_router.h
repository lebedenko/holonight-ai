#pragma once

#include "holonight_application/tools/tool_registry.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <holonight_config/provider_config.h>
#include <holonight_domain/holonight_domain.h>
#include <holonight_providers/anthropic_provider.h>
#include <holonight_providers/google_provider.h>
#include <holonight_providers/ollama_provider.h>
#include <holonight_providers/openai_provider.h>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace holonight_application {

using ProviderAdapter = std::variant<
    std::shared_ptr<holonight_providers::OllamaProvider>, std::shared_ptr<holonight_providers::OpenAIProvider>,
    std::shared_ptr<holonight_providers::AnthropicProvider>, std::shared_ptr<holonight_providers::GoogleProvider>>;

[[nodiscard]] std::optional<ProviderAdapter> createProviderAdapter(
    const holonight_config::ProviderInstanceConfig& config,
    const std::shared_ptr<holonight_providers::HttpClient>& httpClient);

class ProviderAdapterRouter : public QObject {
  Q_OBJECT

 public:
  // `tool_registry` is optional (REQ-F-009): routers that never talk to an Anthropic instance with
  // tool-calling enabled (utility-task routers, provider-management routers) may pass nullptr and
  // sendChat() falls back to never sending a tools[] array, matching pre-tool-calling behavior.
  explicit ProviderAdapterRouter(std::shared_ptr<ToolRegistry> tool_registry = nullptr, QObject* parent = nullptr)
      : QObject(parent), tool_registry_(std::move(tool_registry)) {}
  [[nodiscard]] bool add(const holonight_config::ProviderInstanceConfig& config,
                         const std::shared_ptr<holonight_providers::HttpClient>& httpClient);
  [[nodiscard]] bool remove(const QString& instanceId);
  [[nodiscard]] bool contains(const QString& instanceId) const;
  [[nodiscard]] bool reconfigure(const holonight_config::ProviderInstanceConfig& config);
  [[nodiscard]] bool setEnabled(const QString& instanceId, bool enabled);
  [[nodiscard]] bool setCredential(const QString& instanceId, const QString& credential);
  [[nodiscard]] bool isEnabled(const QString& instanceId) const;
  [[nodiscard]] qsizetype activeStreamCount(const QString& instanceId) const;
  [[nodiscard]] bool canDelete(const QString& instanceId) const;

  [[nodiscard]] const std::vector<holonight_domain::ModelId>* availableModels(const QString& instanceId) const;
  [[nodiscard]] bool restoreAvailableModels(const QString& instanceId, std::vector<holonight_domain::ModelId> models);
  [[nodiscard]] bool refresh(const QString& instanceId, const std::function<void()>& onSuccess = {},
                             const std::function<void(const QString&)>& onError = {});
  [[nodiscard]] holonight_providers::HttpRequestHandlePtr sendChat(
      const holonight_domain::ModelId& model, const std::vector<holonight_domain::Message>& history,
      const std::function<void(const holonight_domain::StreamEvent&)>& onEvent,
      std::chrono::milliseconds idleTimeout = std::chrono::seconds{30});
  static void cancel(const holonight_providers::HttpRequestHandlePtr& request);

 Q_SIGNALS:
  void activeStreamCountChanged(QString instanceId);

 private:
  struct RuntimeRecord {
    ProviderAdapter adapter;
    holonight_config::ProviderInstanceConfig config;
    bool enabled = true;
    qsizetype active_stream_count = 0;
  };

  QHash<QString, RuntimeRecord> records_;
  std::shared_ptr<ToolRegistry> tool_registry_;
};

}  // namespace holonight_application
