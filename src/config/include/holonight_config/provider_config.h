#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace holonight_config {

// Non-secret Ollama configuration persisted to config.json (REQ-F-016, REQ-C-003). No auth token
// field — the token is Secret Service's exclusive responsibility (REQ-C-003). Default member
// initializers double as REQ-F-015's built-in defaults for a missing file/section/field, and as
// REQ-F-014's factory-reset values.
struct OllamaProviderConfig {
  QString base_url = QStringLiteral("http://localhost:11434");
  QString default_model;  // empty ⇒ "no default model saved" (REQ-F-006)
  int context_window = 4096;
  double temperature = 0.7;
  bool tool_calling_enabled = false;

  friend bool operator==(const OllamaProviderConfig&, const OllamaProviderConfig&) = default;
};

// Non-secret OpenAI configuration persisted to config.json (REQ-F-033/034). No auth token field —
// the token is Secret Service's exclusive responsibility (REQ-NF-006). No context_window field —
// the Responses API has no equivalent request parameter (REQ-C-008).
struct OpenAIProviderConfig {
  QString base_url = QStringLiteral("https://api.openai.com/v1");
  QString default_model;  // empty ⇒ "no default model saved"
  double temperature = 1.0;
  bool tool_calling_enabled = false;

  friend bool operator==(const OpenAIProviderConfig&, const OpenAIProviderConfig&) = default;
};

// Non-secret Anthropic configuration persisted to config.json (REQ-F-033/034). No auth key field —
// Secret Service's exclusive responsibility (REQ-F-035/REQ-NF-006). max_output_tokens is required
// (REQ-F-007) — unlike OpenAIProviderConfig, which has no such field (REQ-C-008 there forbids one).
struct AnthropicProviderConfig {
  QString base_url = QStringLiteral("https://api.anthropic.com");
  QString default_model;  // empty ⇒ "no default model saved"
  double temperature = 1.0;
  int max_output_tokens = 1024;
  // REQ-F-009/REQ-F-010: whether the tools[] array (ListFiles et al.) is sent to the Anthropic
  // Messages API at all. Defaults to false for both new and pre-existing instances -- tool-calling
  // must never activate without explicit user opt-in.
  bool tool_calling_enabled = false;

  friend bool operator==(const AnthropicProviderConfig&, const AnthropicProviderConfig&) = default;
};

// Non-secret Google configuration persisted to config.json (REQ-F-034/037). No auth key field —
// Secret Service's exclusive responsibility (REQ-F-036/REQ-NF-006). max_output_tokens is present
// (REQ-F-006), same shape as AnthropicProviderConfig; temperature's valid range is 0.0-2.0 (the
// widest of the four providers), enforced by GoogleProviderSettingsController::save(), not here.
// max_output_tokens defaults higher than the other providers' 1024: Gemini's "thinking" models
// (e.g. gemini-*-flash-latest) spend part of this same budget on invisible reasoning tokens before
// any visible answer text, so 1024 routinely gets exhausted by thinking alone, truncating the
// visible reply via finishReason "MAX_TOKENS" with no visible error (found via a real user report).
struct GoogleProviderConfig {
  QString base_url = QStringLiteral("https://generativelanguage.googleapis.com");
  QString default_model;  // empty ⇒ "no default model saved"
  double temperature = 1.0;
  int max_output_tokens = 8192;
  // Mirrors AnthropicProviderConfig::tool_calling_enabled exactly: whether the tools[] array
  // (ListFiles et al.) is sent to Gemini's streamGenerateContent API at all. Defaults to false for
  // both new and pre-existing instances -- tool-calling must never activate without explicit user
  // opt-in.
  bool tool_calling_enabled = false;

  friend bool operator==(const GoogleProviderConfig&, const GoogleProviderConfig&) = default;
};

enum class ProviderType : std::uint8_t { Ollama, OpenAi, Anthropic, Google };

[[nodiscard]] QString providerTypeToString(ProviderType type);
[[nodiscard]] std::optional<ProviderType> providerTypeFromString(const QString& value);
[[nodiscard]] QString defaultProviderDisplayName(ProviderType type);

using ProviderSettings =
    std::variant<OllamaProviderConfig, OpenAIProviderConfig, AnthropicProviderConfig, GoogleProviderConfig>;

struct ProviderInstanceConfig {
  QString id;
  ProviderType type = ProviderType::Ollama;
  QString display_name;
  bool enabled = true;
  ProviderSettings settings = OllamaProviderConfig{};

  friend bool operator==(const ProviderInstanceConfig&, const ProviderInstanceConfig&) = default;
};

struct ProviderTombstone {
  QString instance_id;
  ProviderType type = ProviderType::Ollama;
  QString last_display_name;

  friend bool operator==(const ProviderTombstone&, const ProviderTombstone&) = default;
};

struct ProviderState {
  std::vector<ProviderInstanceConfig> instances;
  std::vector<ProviderTombstone> tombstones;

  friend bool operator==(const ProviderState&, const ProviderState&) = default;
};

}  // namespace holonight_config
