#include "holonight_providers/openai_tool_codec.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace holonight_providers {
namespace {

QString role(const holonight_domain::MessageRole value) {
  switch (value) {
    case holonight_domain::MessageRole::System:
      return QStringLiteral("system");
    case holonight_domain::MessageRole::Assistant:
      return QStringLiteral("assistant");
    case holonight_domain::MessageRole::User:
      return QStringLiteral("user");
  }
  return QStringLiteral("user");
}

QString itemIdentity(const QJsonObject& item) {
  const QString id = item.value(QStringLiteral("id")).toString();
  return id.isEmpty() ? QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Compact)) : id;
}

}  // namespace

QJsonArray OpenAIToolCodec::encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog) {
  QJsonArray tools;
  for (const auto& definition : catalog.client_tools) {
    tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                             {QStringLiteral("name"), definition.function_name},
                             {QStringLiteral("description"), definition.description},
                             {QStringLiteral("parameters"), definition.input_schema}});
  }
  return tools;
}

QJsonArray OpenAIToolCodec::encodeHistory(const std::vector<holonight_domain::Message>& history) {
  QJsonArray input;
  QSet<QString> replayedContext;
  for (const auto& message : history) {
    if (message.toolCalls().empty()) {
      input.append(
          QJsonObject{{QStringLiteral("role"), role(message.role())}, {QStringLiteral("content"), message.text()}});
      continue;
    }
    for (const auto& entry : message.toolCalls()) {
      for (const auto& context : entry.provider_context) {
        const QString identity = itemIdentity(context);
        if (!replayedContext.contains(identity)) {
          replayedContext.insert(identity);
          input.append(context);
        }
      }
      if (entry.kind == holonight_domain::ToolCallKind::Invocation) {
        QJsonObject item{
            {QStringLiteral("type"), QStringLiteral("function_call")},
            {QStringLiteral("call_id"), entry.tool_use_id},
            {QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
            {QStringLiteral("arguments"),
             QString::fromUtf8(QJsonDocument(entry.input).toJson(QJsonDocument::Compact))}};
        if (entry.provider_item_id.has_value()) {
          item[QStringLiteral("id")] = *entry.provider_item_id;
        }
        input.append(item);
      } else {
        input.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function_call_output")},
            {QStringLiteral("call_id"), entry.tool_use_id},
            {QStringLiteral("output"), QString::fromUtf8(QJsonDocument(entry.result).toJson(QJsonDocument::Compact))}});
      }
    }
  }
  return input;
}

std::expected<std::vector<holonight_domain::ToolRequestEvent>, QString> OpenAIToolCodec::decodeRequests(
    const QString& provider_instance_id, const QJsonArray& output) {
  std::vector<QJsonObject> context;
  std::vector<holonight_domain::ToolRequestEvent> requests;
  for (const auto& value : output) {
    const QJsonObject item = value.toObject();
    if (item.value(QStringLiteral("type")).toString() == QStringLiteral("reasoning") &&
        item.contains(QStringLiteral("encrypted_content"))) {
      context.push_back(item);
    }
  }
  for (const auto& value : output) {
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject item = value.toObject();
    const QString type = item.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("function_call")) {
      continue;
    }
    const QString name = item.value(QStringLiteral("name")).toString();
    const QString callId = item.value(QStringLiteral("call_id")).toString();
    if (name.isEmpty() || callId.isEmpty()) {
      return std::unexpected(QStringLiteral("Malformed OpenAI function call: missing name or call_id"));
    }
    const QJsonValue argumentsValue = item.value(QStringLiteral("arguments"));
    QJsonParseError error{};
    const QJsonDocument arguments = QJsonDocument::fromJson(argumentsValue.toString().toUtf8(), &error);
    if (!argumentsValue.isString() || error.error != QJsonParseError::NoError || !arguments.isObject()) {
      return std::unexpected(
          QStringLiteral("Malformed OpenAI function call '%1': arguments must be a JSON object").arg(name));
    }
    std::optional<QString> itemId;
    const QString id = item.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      itemId = id;
    }
    requests.push_back(holonight_domain::ToolRequestEvent{
        .provider_call_id = callId,
        .provider_instance_id = provider_instance_id,
        .function_name = name,
        .arguments = arguments.object(),
        .provider_item_id = itemId,
        .provider_context = context,
    });
  }
  return requests;
}

}  // namespace holonight_providers
