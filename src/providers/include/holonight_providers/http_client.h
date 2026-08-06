#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace holonight_providers {

enum class HttpMethod : std::uint8_t { Get, Post };

struct HttpRequest {
  HttpMethod method = HttpMethod::Get;
  QString url;
  QByteArray body;
  QString content_type = QStringLiteral("application/json");
  // Extra headers beyond Content-Type — currently only Authorization: Bearer <token>
  // (OllamaProvider::setAuthToken()). Empty by default; independent of content_type, which keeps
  // its own dedicated field for backward compatibility with every existing call site.
  QHash<QString, QString> headers;
};

class HttpRequestHandle {
 public:
  HttpRequestHandle() = default;
  virtual ~HttpRequestHandle() = default;
  HttpRequestHandle(const HttpRequestHandle&) = delete;
  HttpRequestHandle& operator=(const HttpRequestHandle&) = delete;
  HttpRequestHandle(HttpRequestHandle&&) = delete;
  HttpRequestHandle& operator=(HttpRequestHandle&&) = delete;

  virtual void cancel() = 0;
};

using HttpRequestHandlePtr = std::shared_ptr<HttpRequestHandle>;

using HttpDataCallback = std::function<void(const QByteArray&)>;
using HttpFinishedCallback = std::function<void()>;
using HttpErrorCallback = std::function<void(const QString&)>;
using HttpBufferedSuccessCallback = std::function<void(const QByteArray&)>;

class HttpClient {
 public:
  HttpClient() = default;
  virtual ~HttpClient() = default;
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;
  HttpClient(HttpClient&&) = delete;
  HttpClient& operator=(HttpClient&&) = delete;

  // Single round-trip request. on_success fires exactly once with the full response body, or
  // on_error fires instead. A positive timeout bounds the whole request; zero keeps the existing
  // no-timeout behavior.
  virtual HttpRequestHandlePtr send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                                    HttpErrorCallback on_error,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) = 0;

  // Streaming request. on_data fires once per chunk of raw bytes received — not necessarily
  // line-aligned. on_finished fires once when the connection closes normally; on_error fires
  // instead of on_finished on failure, non-2xx HTTP status, or idle timeout. idle_timeout resets
  // on every on_data invocation.
  virtual HttpRequestHandlePtr sendStreaming(const HttpRequest& request, std::chrono::milliseconds idle_timeout,
                                             HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                             HttpErrorCallback on_error) = 0;
};

}  // namespace holonight_providers
