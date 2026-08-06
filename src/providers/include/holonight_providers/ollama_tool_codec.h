#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <expected>
#include <holonight_domain/holonight_domain.h>
#include <vector>

namespace holonight_providers {

// Owns every Ollama /api/chat wire representation used by local tool calling -- mirrors
// GoogleToolCodec's/OpenAIToolCodec's role for their adapters. The application boundary stays
// provider-neutral.
class OllamaToolCodec {
 public:
  // Full "tools" array value for the request body: empty when catalog.client_tools is empty
  // (sendChat() then omits the "tools" key entirely, REQ-F-002); otherwise one
  // {"type":"function","function":{"name","description","parameters"}} object per tool, in
  // registration order.
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);

  // Builds Ollama's flat "messages" array from `history` (REQ-F-007/018). Unlike Google, Ollama's
  // wire format keeps System-role messages inline (no hoisting). Consecutive Assistant-role
  // messages (plain text and/or Invocation tool-calls, all from the same original model turn) are
  // grouped into one wire {"role":"assistant", ...} object with a merged "tool_calls" array;
  // Result-kind entries are never grouped with each other -- each becomes its own standalone
  // {"role":"tool", ...} message, matching REQ-F-007's literal one-message-per-result shape.
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history);

  // One ToolRequestEvent per entry of `tool_calls` (REQ-F-003/004/019), in array order
  // (REQ-NF-004). `tool_calls` is the *already-extracted* `message.tool_calls` QJsonArray from one
  // NDJSON line -- the caller (OllamaProvider::processLine()) is responsible for locating that
  // array; this function only validates and decodes its entries. Returns an error (not a partial
  // vector) if ANY entry is malformed -- REQ-F-003's acceptance criterion ("malformed JSON in
  // arguments causes the parser to emit a stream error, not a malformed ToolRequestEvent") requires
  // all-or-nothing emission, the same contract OpenAIToolCodec::decodeRequests() already has.
  [[nodiscard]] static std::expected<std::vector<holonight_domain::ToolRequestEvent>, QString> decodeRequests(
      const QString& provider_instance_id, const QJsonArray& tool_calls);
};

}  // namespace holonight_providers
