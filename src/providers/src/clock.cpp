#include "holonight_providers/clock.h"

namespace holonight_providers {

std::chrono::milliseconds SteadyClock::now() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

}  // namespace holonight_providers
