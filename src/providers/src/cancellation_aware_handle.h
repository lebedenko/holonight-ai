#pragma once

#include "holonight_providers/http_client.h"

#include <functional>
#include <utility>

namespace holonight_providers::detail {

class CancellationAwareHandle final : public HttpRequestHandle {
 public:
  CancellationAwareHandle(HttpRequestHandlePtr request, std::function<void()> on_cancel)
      : request_(std::move(request)), on_cancel_(std::move(on_cancel)) {}

  void cancel() override {
    if (cancelled_) {
      return;
    }
    cancelled_ = true;
    if (on_cancel_) {
      on_cancel_();
    }
    if (request_) {
      request_->cancel();
    }
  }

 private:
  HttpRequestHandlePtr request_;
  std::function<void()> on_cancel_;
  bool cancelled_ = false;
};

}  // namespace holonight_providers::detail
