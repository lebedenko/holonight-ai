#pragma once

#include <QString>

namespace holonight_config {

// REQ-C-002: $XDG_CONFIG_HOME/holonight-ai/config.json via QStandardPaths::GenericConfigLocation
// (Qt's own XDG implementation — already falls back to ~/.config when XDG_CONFIG_HOME is unset).
// Creates the holonight-ai/ directory if missing; mkpath() is a no-op, not an error, if it already
// exists — mirrors holonight_persistence::resolveDatabaseFilePath() exactly.
[[nodiscard]] QString resolveConfigFilePath();

}  // namespace holonight_config
