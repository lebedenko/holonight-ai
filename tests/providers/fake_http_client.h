#pragma once

#include "holonight_providers/http_client.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <deque>
#include <utility>

namespace holonight_providers {

// Test double for HttpClient (REQ-C-001). Every callback fires synchronously and inline — no
// event loop, no real timers, no network I/O — so tests are deterministic and fast.
class FakeHttpClient : public HttpClient {
 public:
  struct StreamingCall {
    HttpRequest request;
    HttpDataCallback on_data;
    HttpFinishedCallback on_finished;
    HttpErrorCallback on_error;
    bool cancelled = false;
  };

  HttpRequestHandlePtr send(const HttpRequest& request, HttpBufferedSuccessCallback on_success,
                            HttpErrorCallback on_error,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override {
    ++buffered_call_count_;
    last_buffered_request_ = request;
    last_buffered_timeout_ = timeout;
    if (buffered_queue_.empty()) {
      on_error(QStringLiteral("FakeHttpClient: no buffered response queued"));
      return std::make_shared<NoopHandle>();
    }
    const BufferedOutcome outcome = std::move(buffered_queue_.front());
    buffered_queue_.pop_front();
    if (defer_buffered_responses_) {
      pending_buffered_calls_.push_back(PendingBufferedCall{
          .outcome = outcome,
          .on_success = std::move(on_success),
          .on_error = std::move(on_error),
      });
      return std::make_shared<NoopHandle>();
    }
    if (outcome.success) {
      on_success(outcome.body);
    } else {
      on_error(outcome.error_message);
    }
    return std::make_shared<NoopHandle>();
  }

  HttpRequestHandlePtr sendStreaming(const HttpRequest& request, std::chrono::milliseconds idle_timeout,
                                     HttpDataCallback on_data, HttpFinishedCallback on_finished,
                                     HttpErrorCallback on_error) override {
    Q_UNUSED(idle_timeout);
    streaming_calls_.push_back(StreamingCall{.request = request,
                                             .on_data = std::move(on_data),
                                             .on_finished = std::move(on_finished),
                                             .on_error = std::move(on_error),
                                             .cancelled = false});
    return std::make_shared<HandleImpl>(this, streaming_calls_.size() - 1);
  }

  void enqueueBufferedSuccess(QByteArray body) {
    buffered_queue_.push_back(BufferedOutcome{.success = true, .body = std::move(body), .error_message = {}});
  }

  void enqueueBufferedError(QString message) {
    buffered_queue_.push_back(BufferedOutcome{.success = false, .body = {}, .error_message = std::move(message)});
  }

  void setBufferedResponsesDeferred(bool deferred) { defer_buffered_responses_ = deferred; }

  void completeNextBufferedCall() {
    PendingBufferedCall call = std::move(pending_buffered_calls_.front());
    pending_buffered_calls_.pop_front();
    if (call.outcome.success) {
      call.on_success(call.outcome.body);
    } else {
      call.on_error(call.outcome.error_message);
    }
  }

  [[nodiscard]] std::size_t streamingCallCount() const { return streaming_calls_.size(); }

  [[nodiscard]] std::size_t bufferedCallCount() const { return buffered_call_count_; }

  [[nodiscard]] const HttpRequest& lastBufferedRequest() const { return last_buffered_request_; }
  [[nodiscard]] std::chrono::milliseconds lastBufferedTimeout() const { return last_buffered_timeout_; }

  [[nodiscard]] const StreamingCall& streamingCall(std::size_t index) const { return streaming_calls_.at(index); }

  void emitData(std::size_t index, const QByteArray& chunk) { streaming_calls_.at(index).on_data(chunk); }

  void emitFinished(std::size_t index) { streaming_calls_.at(index).on_finished(); }

  void emitError(std::size_t index, const QString& message) { streaming_calls_.at(index).on_error(message); }

  void simulateIdleTimeout(std::size_t index) {
    emitError(index, QStringLiteral("Provider did not respond within the configured idle timeout"));
  }

 private:
  struct BufferedOutcome {
    bool success = true;
    QByteArray body;
    QString error_message;
  };

  struct PendingBufferedCall {
    BufferedOutcome outcome;
    HttpBufferedSuccessCallback on_success;
    HttpErrorCallback on_error;
  };

  class HandleImpl : public HttpRequestHandle {
   public:
    HandleImpl(FakeHttpClient* owner, std::size_t index) : owner_(owner), index_(index) {}

    void cancel() override { owner_->streaming_calls_.at(index_).cancelled = true; }

   private:
    FakeHttpClient* owner_;
    std::size_t index_;
  };

  class NoopHandle : public HttpRequestHandle {
   public:
    void cancel() override {}
  };

  std::deque<BufferedOutcome> buffered_queue_;
  std::deque<PendingBufferedCall> pending_buffered_calls_;
  std::deque<StreamingCall> streaming_calls_;
  bool defer_buffered_responses_ = false;
  std::size_t buffered_call_count_ = 0;
  HttpRequest last_buffered_request_;
  std::chrono::milliseconds last_buffered_timeout_{};
};

}  // namespace holonight_providers
