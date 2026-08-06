#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <holonight_domain/holonight_domain.h>
#include <optional>

namespace holonight_providers {

// Owns every Gemini wire representation used by local tool calling -- mirrors AnthropicToolCodec's
// role for the Google adapter. The application boundary stays provider-neutral.
class GoogleToolCodec {
 public:
  // Full "tools" array value for the request body: empty when catalog.client_tools is empty
  // (sendChat() then omits the "tools" key entirely); otherwise a single-element array
  // [{"functionDeclarations": [...]}] per Gemini's Tool schema.
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);

  // Builds Gemini's "contents" array from `history`, hoisting System-role messages into
  // `system_parts` and reconstructing functionCall/functionResponse parts from ToolCallEntry.
  // Consecutive same-role Messages are grouped into one Content entry with multiple parts.
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history,
                                                QStringList& system_parts);

  // One ToolRequestEvent per functionCall Part object. `part` is the *Part*-level JSON object -- the
  // object with sibling keys "functionCall" and, optionally, "thoughtSignature" -- not the inner
  // functionCall object alone. Returns std::nullopt when the part is malformed (missing "name", or
  // "args" present but not a JSON object) so the caller can fail the stream instead of emitting a
  // garbage event.
  [[nodiscard]] static std::optional<holonight_domain::ToolRequestEvent> decodeRequest(QString provider_instance_id,
                                                                                       const QJsonObject& part);

  // functionResponse Part for one Result-kind ToolCallEntry. Omits "id" when
  // entry.provider_call_id_synthesized is true; includes it verbatim (from entry.tool_use_id)
  // otherwise.
  [[nodiscard]] static QJsonObject encodeFunctionResponse(const holonight_domain::ToolCallEntry& entry);
};

}  // namespace holonight_providers
