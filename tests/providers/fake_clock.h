#pragma once

#include "holonight_providers/clock.h"

#include <QtGlobal>

#include <chrono>
#include <deque>

namespace holonight_providers {

// Test double for Clock (REQ-NF-001). now() returns queued values in FIFO order; if the queue is
// empty it keeps returning the last value returned (or zero, if nothing was ever queued).
class FakeClock : public Clock {
 public:
  FakeClock& push(qint64 milliseconds) {
    queue_.emplace_back(milliseconds);
    return *this;
  }

  [[nodiscard]] std::chrono::milliseconds now() const override {
    if (queue_.empty()) {
      return last_;
    }
    last_ = queue_.front();
    queue_.pop_front();
    return last_;
  }

 private:
  mutable std::deque<std::chrono::milliseconds> queue_;
  mutable std::chrono::milliseconds last_{};
};

}  // namespace holonight_providers
