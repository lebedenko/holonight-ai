#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <holonight_domain/holonight_domain.h>

namespace holonight_providers {

// Owns every Anthropic wire representation used by local tool calling. The application boundary
// remains provider-neutral; other providers can add codecs without changing tool packages or QML.
class AnthropicToolCodec {
 public:
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history,
                                                QStringList& system_parts);
  [[nodiscard]] static QJsonObject encodeLocalResult(const holonight_domain::ToolCallEntry& entry);
  [[nodiscard]] static holonight_domain::ToolRequestEvent decodeRequest(QString provider_instance_id,
                                                                        QString provider_call_id, QString function_name,
                                                                        QJsonObject arguments,
                                                                        bool provider_hosted = false);
};

}  // namespace holonight_providers
