#pragma once

#include "holonight_providers/http_client.h"

#include <QHash>
#include <QString>

#include <chrono>
#include <functional>
#include <holonight_domain/holonight_domain.h>
#include <memory>
#include <vector>

namespace holonight_providers {

// Adapter for Google's Gemini Developer API (POST /v1beta/models/{model}:streamGenerateContent
// ?alt=sse — see DESIGN.md's Corrections to SPEC.md for why the action suffix and alt=sse query
// parameter are required, not the literal :generateContent SPEC.md names). Deliberately a plain
// concrete class, structurally parallel to OllamaProvider/OpenAIProvider/AnthropicProvider but
// sharing no base class with any of them (REQ-F-040/SPEC.md §F.9 — no virtual base, no
// dynamic-dispatch interface for this cycle; see DESIGN.md §5.1).
class GoogleProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once ready to receive the async result.
  explicit GoogleProvider(std::shared_ptr<HttpClient> http_client,
                          QString base_url = QStringLiteral("https://generativelanguage.googleapis.com"),
                          QString instance_id = QStringLiteral("google"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;
  void restoreAvailableModels(std::vector<holonight_domain::ModelId> models);

  // Re-fetches GET /v1beta/models (denylist + supportedGenerationMethods filtered, REQ-F-009/010)
  // and replaces the cached list. Model IDs are stored WITHOUT the "models/" resource-name prefix
  // Gemini's API returns (DESIGN.md §5.8) — availableModels() always yields bare IDs like
  // "gemini-2.0-flash".
  void refresh(const std::function<void()>& on_complete = {});
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call (REQ-F-001).
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // x-goog-api-key support (REQ-F-001/038). Named setAuthKey() per REQ-F-040's explicit method-
  // surface list, matching AnthropicProvider's naming (a raw API-key header, not a Bearer token).
  // Empty (default) ⇒ no x-goog-api-key header is sent at all — REQ-F-039 explicitly mirrors
  // OllamaProvider's "omit the header entirely" behavior here, NOT Anthropic's "always send one
  // unconditional header" shape (Google has no unconditional second header like anthropic-version).
  // Never logged, never exposed via a getter — write-only by design (REQ-NF-006).
  void setAuthKey(QString auth_key);

  // Applied to every subsequent sendChat()'s generationConfig.temperature field. Range 0.0–2.0
  // (REQ-F-005) — the widest of the four providers. NOT validated here (the provider trusts its
  // caller; range enforcement is the settings controller's job, REQ-C-003 forbids provider-level
  // model-specific validation).
  void setTemperature(double temperature);

  // Applied to every subsequent sendChat()'s generationConfig.maxOutputTokens field (REQ-F-006).
  // Default 1024 matches holonight_config::GoogleProviderConfig{}'s own default.
  void setMaxOutputTokens(int max_output_tokens);

  // Sends `history` to {model}:streamGenerateContent?alt=sse and streams the response as
  // StreamEvents (SSE framing, DESIGN.md §4). System-role messages in `history` are hoisted to the
  // top-level "systemInstruction" field, not sent inline (REQ-F-003/004) — the only behavioral
  // divergence from Ollama/OpenAI's sendChat() at the call-site level; the signature itself is
  // identical, and `model.model_name` goes into the URL path, never into the JSON body (DESIGN.md
  // §5.9).
  // Optional 5th `tool_catalog` parameter mirrors AnthropicProvider::sendChat() exactly --
  // default-constructed ToolCatalogSnapshot{} keeps every existing call site compiling and
  // behaving unchanged.
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                                const holonight_domain::ToolCatalogSnapshot& tool_catalog = {});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);
  void fetchModelPage(const QString& page_token,
                      const std::shared_ptr<std::vector<holonight_domain::ModelId>>& accumulated_models,
                      const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Single conditional header (empty map ⇒ no auth header at all, REQ-F-039) — same contract as
  // OllamaProvider's/OpenAIProvider's authHeaders(), unlike AnthropicProvider's requestHeaders()
  // (which always sends one unconditional header). Named authHeaders() to match that contract
  // directly, not requestHeaders() (DESIGN.md §5.4).
  [[nodiscard]] QHash<QString, QString> authHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString instance_id_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_key_;
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
};

}  // namespace holonight_providers
