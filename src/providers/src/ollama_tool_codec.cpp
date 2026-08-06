#include "holonight_providers/ollama_tool_codec.h"

#include <QJsonDocument>
#include <QUuid>

namespace holonight_providers {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageRole;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

QString roleToOllamaString(MessageRole role) {
  switch (role) {
    case MessageRole::System:
      return QStringLiteral("system");
    case MessageRole::User:
      return QStringLiteral("user");
    case MessageRole::Assistant:
      return QStringLiteral("assistant");
  }
  return QStringLiteral("user");
}

}  // namespace

QJsonArray OllamaToolCodec::encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog) {
  QJsonArray tools;
  for (const auto& definition : catalog.client_tools) {
    tools.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), definition.function_name},
                                                 {QStringLiteral("description"), definition.description},
                                                 {QStringLiteral("parameters"), definition.input_schema}}}});
  }
  return tools;  // empty catalog -> empty array, REQ-F-017's own acceptance criterion verbatim.
}

QJsonArray OllamaToolCodec::encodeHistory(const std::vector<Message>& history) {
  QJsonArray messages;
  bool hasOpenAssistantGroup = false;
  QString groupText;
  QJsonArray groupToolCalls;

  auto flush = [&] {
    if (!hasOpenAssistantGroup) {
      return;
    }
    QJsonObject entry{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), groupText}};
    if (!groupToolCalls.isEmpty()) {
      entry[QStringLiteral("tool_calls")] = groupToolCalls;
    }
    messages.append(entry);
    hasOpenAssistantGroup = false;
    groupText.clear();
    groupToolCalls = QJsonArray{};
  };

  for (const Message& message : history) {
    if (!message.toolCalls().empty() && message.toolCalls().front().kind == ToolCallKind::Result) {
      flush();
      for (const auto& entry : message.toolCalls()) {
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("tool")},
            {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(entry.result).toJson(QJsonDocument::Compact))},
            {QStringLiteral("tool_name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name}});
      }
      continue;
    }

    if (message.role() != MessageRole::Assistant) {
      flush();
      messages.append(QJsonObject{{QStringLiteral("role"), roleToOllamaString(message.role())},
                                  {QStringLiteral("content"), message.text()}});
      continue;
    }

    // Assistant-mapped: plain text, or an Invocation-carrying message. Consecutive Assistant
    // messages merge into one open group (§3.2.2 of DESIGN.md).
    if (!hasOpenAssistantGroup) {
      hasOpenAssistantGroup = true;
      groupText = message.text();
    } else if (!message.text().isEmpty()) {
      groupText += message.text();
    }
    for (const ToolCallEntry& entry : message.toolCalls()) {
      groupToolCalls.append(QJsonObject{
          {QStringLiteral("function"),
           QJsonObject{{QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
                       {QStringLiteral("arguments"), entry.input}}}});
    }
  }
  flush();
  return messages;
}

std::expected<std::vector<holonight_domain::ToolRequestEvent>, QString> OllamaToolCodec::decodeRequests(
    const QString& provider_instance_id, const QJsonArray& tool_calls) {
  std::vector<holonight_domain::ToolRequestEvent> events;
  events.reserve(static_cast<std::size_t>(tool_calls.size()));

  for (const auto& callValue : tool_calls) {
    const QJsonObject call = callValue.toObject();
    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    const QString name = function.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      return std::unexpected(QStringLiteral("Ollama tool call missing function name"));
    }

    const QJsonValue argumentsValue = function.value(QStringLiteral("arguments"));
    if (!argumentsValue.isObject()) {
      // REQ-F-006: arguments must be a pre-parsed JSON object, never a JSON-encoded string.
      return std::unexpected(QStringLiteral("Ollama tool call arguments are not a JSON object"));
    }

    QString providerCallId;
    bool synthesized = false;
    const QString rawId = call.value(QStringLiteral("id")).toString();
    if (!rawId.isEmpty()) {
      providerCallId = rawId;
    } else {
      providerCallId = QUuid::createUuid().toString(QUuid::WithoutBraces);  // REQ-F-005
      synthesized = true;
    }

    events.push_back(holonight_domain::ToolRequestEvent{
        .provider_call_id = providerCallId,
        .provider_instance_id = provider_instance_id,
        .function_name = name,
        .arguments = argumentsValue.toObject(),
        .execution_location = holonight_domain::ToolExecutionLocation::LocalClient,
        .source = holonight_domain::ToolSource::BuiltIn,
        .provider_call_id_synthesized = synthesized,
    });
  }

  return events;  // REQ-NF-004: vector order == tool_calls[] array order, by construction.
}

}  // namespace holonight_providers
