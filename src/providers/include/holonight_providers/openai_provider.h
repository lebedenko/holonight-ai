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

// Adapter for OpenAI's Responses API (POST /v1/responses). Deliberately a plain concrete class,
// structurally parallel to OllamaProvider but not sharing a base class with it — see
// DESIGN.md §5.1 for why a virtual provider interface was rejected for exactly two providers.
class OpenAIProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once they are ready to receive the
  // asynchronous result. This avoids construction-time requests racing with UI initialization.
  explicit OpenAIProvider(std::shared_ptr<HttpClient> http_client,
                          QString base_url = QStringLiteral("https://api.openai.com/v1"),
                          QString instance_id = QStringLiteral("openai"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;
  void restoreAvailableModels(std::vector<holonight_domain::ModelId> models);

  // Re-fetches /v1/models (denylist-filtered) and replaces the cached list. on_complete
  // (optional) fires once the fetch settles, success or failure.
  void refresh(const std::function<void()>& on_complete = {});

  // Distinguishes success from failure, unlike the single-callback overload above. Both overloads
  // funnel into the same private fetchModelList(on_success, on_error).
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call. Already in-flight
  // requests are unaffected — base_url_ is only read when building a new HttpRequest.
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // Bearer-token support. Empty (default) ⇒ no Authorization header is sent (REQ-F-037). Never
  // logged, never exposed via a getter — write-only by design.
  void setAuthToken(QString auth_token);

  // Applied to every subsequent sendChat()'s JSON body. Default (1.0) matches
  // holonight_config::OpenAIProviderConfig{}'s own default. No context-window setter exists — the
  // Responses API has no equivalent request parameter (REQ-C-008).
  void setTemperature(double temperature);

  // Sends `history` to /v1/responses and streams the response as StreamEvents (SSE framing).
  HttpRequestHandlePtr sendChat(const holonight_domain::ModelId& model,
                                const std::vector<holonight_domain::Message>& history,
                                const std::function<void(const holonight_domain::StreamEvent&)>& on_event,
                                std::chrono::milliseconds idle_timeout = std::chrono::seconds{30},
                                const holonight_domain::ToolCatalogSnapshot& tool_catalog = {});

 private:
  void fetchModelList(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);
  [[nodiscard]] QHash<QString, QString> authHeaders() const;

  std::shared_ptr<HttpClient> http_client_;
  QString instance_id_;
  QString base_url_;
  std::vector<holonight_domain::ModelId> available_models_;
  QString auth_token_;
  double temperature_ = 1.0;
};

}  // namespace holonight_providers
