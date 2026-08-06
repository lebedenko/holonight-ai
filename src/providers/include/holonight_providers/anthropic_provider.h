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

// Adapter for Anthropic's Messages API (POST /v1/messages). Deliberately a plain concrete class,
// structurally parallel to OllamaProvider/OpenAIProvider but sharing no base class with either
// (REQ-F-039 — no virtual base, no dynamic-dispatch interface for this cycle; see DESIGN.md §5.1 for
// why this holds even now that a third such provider exists).
class AnthropicProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once ready to receive the async result.
  explicit AnthropicProvider(std::shared_ptr<HttpClient> http_client,
                             QString base_url = QStringLiteral("https://api.anthropic.com"),
                             QString instance_id = QStringLiteral("anthropic"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;
  void restoreAvailableModels(std::vector<holonight_domain::ModelId> models);

  // Re-fetches GET /v1/models (denylist-filtered, REQ-F-010/011) and replaces the cached list.
  void refresh(const std::function<void()>& on_complete = {});
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call (REQ-F-001).
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // x-api-key support (REQ-F-001/038). Named setAuthKey(), not setAuthToken() — Anthropic's scheme is
  // a raw API key header, not a Bearer token (REQ-F-039's method-surface list names this explicitly).
  // Empty (default) ⇒ no x-api-key header is sent; anthropic-version is still sent regardless
  // (REQ-F-002). Never logged, never exposed via a getter — write-only by design (REQ-NF-006).
  void setAuthKey(QString auth_key);

  // Applied to every subsequent sendChat()'s "temperature" field. Range 0.0–1.0 (REQ-F-006) — NOT
  // validated here (the provider trusts its caller; range enforcement is the settings controller's
  // job, REQ-C-003 forbids provider-level model-specific validation).
  void setTemperature(double temperature);

  // Applied to every subsequent sendChat()'s required "max_tokens" field (REQ-F-007). Default 1024
  // matches holonight_config::AnthropicProviderConfig{}'s own default.
  void setMaxOutputTokens(int max_output_tokens);

  // Sends `history` to /v1/messages and streams the response as StreamEvents (SSE framing, §4).
  // System-role messages in `history` are hoisted to the top-level "system" field, not sent inline
  // (REQ-F-004/005) — the only behavioral divergence from Ollama/OpenAI's sendChat() at the call-site
  // level; the signature itself is identical. `tools` defaults to empty, in which case the request
  // body omits the "tools" field entirely (call sites/tests that predate tool-calling keep compiling
  // and behaving unchanged); when non-empty it is passed through verbatim as tools[] (REQ-F-009).
  // `history` entries carrying tool_calls() are reconstructed as tool_use/tool_result content blocks,
  // with consecutive same-role entries grouped into a single Anthropic API message (see .cpp).
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                                const holonight_domain::ToolCatalogSnapshot& tool_catalog = {});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Always includes anthropic-version (REQ-F-002, every request including model-discovery GETs);
  // includes x-api-key only when auth_key_ is non-empty (REQ-F-038). Two headers instead of the
  // single conditional Authorization header Ollama/OpenAI's authHeaders() returns — hence the
  // renamed method (requestHeaders(), not authHeaders()): the anthropic-version entry is present
  // unconditionally, so "auth" is no longer an accurate name for what this returns.
  [[nodiscard]] QHash<QString, QString> requestHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString instance_id_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_key_;
  double temperature_ = 1.0;
  int max_output_tokens_ = 1024;
};

}  // namespace holonight_providers
