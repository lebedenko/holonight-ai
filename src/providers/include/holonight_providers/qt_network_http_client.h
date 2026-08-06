#pragma once

#include "holonight_providers/http_client.h"

#include <QNetworkAccessManager>
#include <QObject>

namespace holonight_providers {

// Production HttpClient backed by Qt6::Network. QObject-derived only because
// QNetworkAccessManager/QNetworkReply are themselves signal-based; this is the sole seam in
// holonight_providers where Qt's signal/slot machinery is used — HttpClient's own public contract
// stays plain-callback.
class QtNetworkHttpClient : public QObject, public HttpClient {
  Q_OBJECT

 public:
  explicit QtNetworkHttpClient(QObject* parent = nullptr);

  HttpRequestHandlePtr send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                            HttpErrorCallback on_error,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override;

  HttpRequestHandlePtr sendStreaming(const HttpRequest& request, std::chrono::milliseconds idle_timeout,
                                     HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                     HttpErrorCallback on_error) override;

 private:
  QNetworkAccessManager network_manager_;
};

}  // namespace holonight_providers
