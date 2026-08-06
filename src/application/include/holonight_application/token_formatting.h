#pragma once

#include <QString>
#include <QVariant>

namespace holonight_application {

// REQ-F-005. `tokens`: an invalid QVariant returns an empty string (caller is expected to omit the
// badge/row entirely in that case, per REQ-F-004/007 -- this function never fabricates a
// placeholder). Values >=1000 are abbreviated as "N.NK" (1 decimal); values <1000 are exact
// integers with no thousands separator.
[[nodiscard]] QString formatTokenCount(const QVariant& tokens);

}  // namespace holonight_application
