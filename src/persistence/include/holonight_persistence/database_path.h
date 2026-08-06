#pragma once

#include <QString>

namespace holonight_persistence {

// REQ-F-001: $XDG_DATA_HOME/holonight-ai/conversations.db, falling back to
// ~/.local/share/holonight-ai/conversations.db. Creates the holonight-ai/ directory if missing
// (mkpath is a no-op, not an error, if it already exists).
[[nodiscard]] QString resolveDatabaseFilePath();

}  // namespace holonight_persistence
