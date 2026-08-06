#pragma once

#include <QJsonArray>
#include <QString>

#include <expected>
#include <holonight_domain/holonight_domain.h>
#include <vector>

namespace holonight_providers {

class OpenAIToolCodec {
 public:
  [[nodiscard]] static QJsonArray encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog);
  [[nodiscard]] static QJsonArray encodeHistory(const std::vector<holonight_domain::Message>& history);
  [[nodiscard]] static std::expected<std::vector<holonight_domain::ToolRequestEvent>, QString> decodeRequests(
      const QString& provider_instance_id, const QJsonArray& output);
};

}  // namespace holonight_providers
