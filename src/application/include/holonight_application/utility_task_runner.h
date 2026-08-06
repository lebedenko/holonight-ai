#pragma once

#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "holonight_providers/anthropic_provider.h"
#include "holonight_providers/google_provider.h"
#include "holonight_providers/http_client.h"
#include "holonight_providers/ollama_provider.h"
#include "holonight_providers/openai_provider.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <holonight_config/utility_config.h>
#include <holonight_credentials/credential_store.h>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <optional>
#include <vector>

namespace holonight_application {

// Production passes the same base URLs used by chat so custom endpoints remain consistent.
// Optional HTTP clients provide a deterministic test seam without sharing provider instances.
struct UtilityProviderEndpoints {
  QString ollama_base_url;
  QString openai_base_url;
  QString anthropic_base_url;
  QString google_base_url;
  std::shared_ptr<holonight_providers::HttpClient> ollama_http_client;
  std::shared_ptr<holonight_providers::HttpClient> openai_http_client;
  std::shared_ptr<holonight_providers::HttpClient> anthropic_http_client;
  std::shared_ptr<holonight_providers::HttpClient> google_http_client;
};

// Owns provider adapters fully isolated from ChatViewModel's adapters (REQ-NF-001), derived from
// the same saved instance configurations. They issue one-shot model calls for background utility
// work — this cycle's sole consumer is asynchronous conversation-title generation. No
// ConversationRepository dependency; only emits titleGenerated on success.
class UtilityTaskRunner : public QObject {
  Q_OBJECT

 public:
  explicit UtilityTaskRunner(holonight_credentials::CredentialStore* credential_store,
                             holonight_config::UtilityConfig utility_config, const UtilityProviderEndpoints& endpoints,
                             QObject* parent = nullptr, holonight_config::ProviderState provider_state = {},
                             const QHash<QString, std::vector<holonight_domain::ModelId>>& available_models = {});

  // Fire-and-forget (REQ-NF-003). No-op if conversationId was already attempted (REQ-NF-004/005).
  // taskOverride is accepted for API-surface completeness only (REQ-F-002's acceptance criterion);
  // any non-nullopt value is logged and ignored this cycle, never thrown (keeps this codebase's
  // zero-crash posture, REQ-U-001/002).
  void requestTitleGeneration(const holonight_domain::ConversationId& conversationId, const QString& firstUserText,
                              const QString& firstAssistantText, const holonight_domain::ModelId& chatFallbackModel,
                              const std::optional<holonight_domain::ModelId>& taskOverride = std::nullopt);

  // Mirrors ChatViewModel::applyProviderState() (REQ-C-001): keeps this runner's router in lockstep
  // whenever Settings persists provider changes. Injects utility-specific generation parameters into
  // a copy of each instance before it reaches the router (REQ-F-016) — provider_state_ itself keeps
  // the unmodified, chat-configured settings.
  void applyProviderState(holonight_config::ProviderState provider_state);

  // Swaps the live UtilityConfig consulted by resolveModel()/requestTitleGeneration(), independent
  // of provider/router changes (REQ-F-020).
  void applyUtilityConfig(holonight_config::UtilityConfig utility_config);

 Q_SIGNALS:
  void titleGenerationStarted(QString conversationId);
  void titleGenerationFinished(QString conversationId);

  // Emitted only on success (REQ-F-007/008): title is non-empty. No failure signal exists —
  // failure is silent by design (REQ-U-001, REQ-C-002).
  void titleGenerated(QString conversationId, QString title);

 private:
  [[nodiscard]] std::optional<holonight_domain::ModelId> resolveModel(
      const std::optional<holonight_domain::ModelId>& taskOverride,
      const holonight_domain::ModelId& chatFallbackModel) const;
  [[nodiscard]] std::optional<holonight_domain::ModelId> resolveModelLegacy(
      const std::optional<holonight_domain::ModelId>& taskOverride,
      const holonight_domain::ModelId& chatFallbackModel) const;
  [[nodiscard]] std::optional<holonight_domain::ModelId> resolveModelViaRouter(
      const std::optional<holonight_domain::ModelId>& taskOverride,
      const holonight_domain::ModelId& chatFallbackModel) const;
  void dispatchGeneration(const QString& conversationKey, const holonight_domain::ModelId& model,
                          const QString& firstUserText, const QString& firstAssistantText);
  [[nodiscard]] holonight_providers::HttpRequestHandlePtr dispatchSendChat(
      const holonight_domain::ModelId& model, const std::vector<holonight_domain::Message>& history,
      const std::function<void(const holonight_domain::StreamEvent&)>& onEvent);
  void continuePendingGeneration(const QString& conversationKey);
  void settleGeneration(const QString& conversationKey);
  void onProviderChanged(const QString& providerId);
  void registerRuntimeProvider(const holonight_config::ProviderInstanceConfig& instance);

  std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
  std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
  std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
  std::shared_ptr<holonight_providers::GoogleProvider> google_provider_;
  std::unique_ptr<ProviderAdapterRouter> adapter_router_;
  std::unique_ptr<ProviderRuntimeCoordinator> provider_runtime_coordinator_;
  holonight_config::UtilityConfig utility_config_;
  holonight_config::ProviderState provider_state_;
  UtilityProviderEndpoints endpoints_;

  struct InFlightGeneration {
    holonight_domain::ModelId model;
    QString first_user_text;
    QString first_assistant_text;
    QString accumulated_text;
    holonight_providers::HttpRequestHandlePtr handle;
    bool settled = false;
  };
  // Entries remain after settlement so a failed task cannot be retried in this process.
  QHash<QString, InFlightGeneration> generations_;  // keyed by conversationId string
};

}  // namespace holonight_application
