#pragma once

#include <holonight_domain/model_id.h>
#include <optional>

namespace holonight_config {

// Non-secret utility-task configuration persisted to config.json (docs/sdd/background-ai-settings-panel).
// Not a per-provider config — lives under its own top-level "utility" key rather than "providers".
// Unset by default so UtilityTaskRunner's model resolution chain always falls through to the
// conversation's active chat model.
struct UtilityConfig {
  std::optional<holonight_domain::ModelId> default_utility_model;

  // Absent (nullopt) means "not yet set by the user" and is treated as `true` everywhere it's
  // consulted — see UtilityTaskRunner::requestTitleGeneration(). Distinguishing "unset" from
  // "explicitly false" lets loadUtilityConfig() tell a legacy config apart from one where the user
  // explicitly disabled the toggle and then re-enabled it.
  std::optional<bool> chat_title_generation_enabled;

  // Task-specific override for chat-title generation. Consulted before default_utility_model in
  // UtilityTaskRunner::resolveModel()'s tier ladder.
  std::optional<holonight_domain::ModelId> chat_title_model_override;

  friend bool operator==(const UtilityConfig&, const UtilityConfig&) = default;
};

}  // namespace holonight_config
