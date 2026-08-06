#pragma once

#include <QString>

#include <atomic>

namespace holonight_persistence {

inline QString uniqueTestConnectionName() {
  static std::atomic<int> counter{0};
  return QStringLiteral("holonight_test_conn_%1").arg(counter.fetch_add(1));
}

}  // namespace holonight_persistence
