#pragma once

#include <chrono>

namespace holonight_providers {

class Clock {
 public:
  Clock() = default;
  virtual ~Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;

  [[nodiscard]] virtual std::chrono::milliseconds now() const = 0;
};

class SteadyClock : public Clock {
 public:
  [[nodiscard]] std::chrono::milliseconds now() const override;
};

}  // namespace holonight_providers
