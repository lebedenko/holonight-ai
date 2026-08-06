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

class OllamaProvider {
 public:
  // Model discovery is explicit: callers invoke refresh() once they are ready to receive the
  // asynchronous result. This avoids construction-time requests racing with UI initialization.
  explicit OllamaProvider(std::shared_ptr<HttpClient> http_client,
                          QString base_url = QStringLiteral("http://localhost:11434"),
                          QString instance_id = QStringLiteral("ollama"));

  [[nodiscard]] const std::vector<holonight_domain::ModelId>& availableModels() const;
  void restoreAvailableModels(std::vector<holonight_domain::ModelId> models);

  // Re-fetches /api/tags and replaces the cached list. on_complete (optional) fires once the
  // fetch settles, success or failure.
  void refresh(const std::function<void()>& on_complete = {});

  // Distinguishes success from failure, unlike the single-callback overload above. Both overloads
  // funnel into the same private fetchModelList(on_success, on_error); this one is new call
  // surface, additive only — no existing caller is touched.
  void refresh(const std::function<void()>& on_success, const std::function<void(const QString&)>& on_error);

  // Reconfigures the base URL used by the NEXT refresh()/sendChat() call. Already in-flight
  // requests are unaffected — base_url_ is only read when building a new HttpRequest.
  void setBaseUrl(QString base_url);
  [[nodiscard]] const QString& baseUrl() const;

  // Bearer-token support. Empty (default) ⇒ no Authorization header is sent. Never logged, never
  // exposed via a getter — write-only by design.
  void setAuthToken(QString auth_token);

  // Applied to every subsequent sendChat()'s JSON "options" object (num_ctx, temperature).
  // Defaults (4096 / 0.7) match holonight_config::OllamaProviderConfig{}'s own defaults.
  void setContextWindow(int context_window);
  void setTemperature(double temperature);

  // Sends `history` to /api/chat and streams the response as StreamEvents. `tool_catalog`
  // (default empty) is encoded into the request's "tools" array when non-empty.
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
  int context_window_ = 4096;
  double temperature_ = 0.7;
};

}  // namespace holonight_providers
