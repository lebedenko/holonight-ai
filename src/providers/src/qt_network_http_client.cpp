#include "holonight_providers/qt_network_http_client.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace holonight_providers {

namespace {

class QNetworkReplyHandle : public HttpRequestHandle {
 public:
  explicit QNetworkReplyHandle(QNetworkReply* reply) : reply_(reply) {}

  void cancel() override {
    if (reply_) {
      reply_->abort();
    }
  }

 private:
  QPointer<QNetworkReply> reply_;
};

QNetworkRequest buildNetworkRequest(const HttpRequest& request) {
  QNetworkRequest networkRequest{QUrl(request.url)};
  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, request.content_type);
  for (auto it = request.headers.constBegin(); it != request.headers.constEnd(); ++it) {
    networkRequest.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
  }
  return networkRequest;
}

QNetworkReply* issueRequest(QNetworkAccessManager& manager, const HttpRequest& request) {
  const QNetworkRequest networkRequest = buildNetworkRequest(request);
  if (request.method == HttpMethod::Post) {
    return manager.post(networkRequest, request.body);
  }
  return manager.get(networkRequest);
}

bool isSuccessfulHttpStatus(QNetworkReply* reply) {
  const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  return statusCode >= 200 && statusCode < 300;
}

}  // namespace

QtNetworkHttpClient::QtNetworkHttpClient(QObject* parent) : QObject(parent) {}

HttpRequestHandlePtr QtNetworkHttpClient::send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                                               HttpErrorCallback on_error, std::chrono::milliseconds timeout) {
  QNetworkReply* reply = issueRequest(network_manager_, request);

  auto resolved = std::make_shared<bool>(false);
  auto* timeoutTimer = new QTimer(reply);
  timeoutTimer->setSingleShot(true);

  if (timeout > std::chrono::milliseconds::zero()) {
    timeoutTimer->setInterval(static_cast<int>(timeout.count()));
    QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply, resolved, on_error]() {
      if (*resolved) {
        return;
      }
      *resolved = true;
      on_error(QStringLiteral("Request timed out"));
      reply->abort();
    });
    timeoutTimer->start();
  }

  QObject::connect(reply, &QNetworkReply::finished, reply, [reply, timeoutTimer, resolved, on_success, on_error]() {
    timeoutTimer->stop();
    if (!*resolved) {
      *resolved = true;
      if (!isSuccessfulHttpStatus(reply)) {
        const QByteArray body = reply->readAll();
        on_error(!body.isEmpty() ? QString::fromUtf8(body) : reply->errorString());
      } else if (reply->error() != QNetworkReply::NoError) {
        on_error(reply->errorString());
      } else {
        on_success(reply->readAll());
      }
    }
    reply->deleteLater();
  });

  return std::make_shared<QNetworkReplyHandle>(reply);
}

HttpRequestHandlePtr QtNetworkHttpClient::sendStreaming(const HttpRequest& request,
                                                        std::chrono::milliseconds idle_timeout,
                                                        HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                                        HttpErrorCallback on_error) {
  QNetworkReply* reply = issueRequest(network_manager_, request);

  auto* idleTimer = new QTimer(reply);
  idleTimer->setSingleShot(true);
  idleTimer->setInterval(static_cast<int>(idle_timeout.count()));

  auto resolved = std::make_shared<bool>(false);

  QObject::connect(idleTimer, &QTimer::timeout, reply, [reply, resolved, on_error]() {
    if (*resolved) {
      return;
    }
    *resolved = true;
    on_error(QStringLiteral("Provider did not respond within the configured idle timeout"));
    reply->abort();
  });

  QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, idleTimer, resolved, on_data]() {
    if (*resolved) {
      return;
    }
    idleTimer->start();
    on_data(reply->readAll());
  });

  QObject::connect(reply, &QNetworkReply::finished, reply, [reply, idleTimer, resolved, on_finished, on_error]() {
    idleTimer->stop();
    if (!*resolved) {
      *resolved = true;
      if (!isSuccessfulHttpStatus(reply)) {
        const QByteArray body = reply->readAll();
        on_error(!body.isEmpty() ? QString::fromUtf8(body) : reply->errorString());
      } else if (reply->error() != QNetworkReply::NoError) {
        on_error(reply->errorString());
      } else {
        on_finished();
      }
    }
    reply->deleteLater();
  });

  idleTimer->start();
  return std::make_shared<QNetworkReplyHandle>(reply);
}

}  // namespace holonight_providers
