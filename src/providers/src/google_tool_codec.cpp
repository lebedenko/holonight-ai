#include "holonight_providers/google_tool_codec.h"

#include <QUuid>

namespace holonight_providers {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageRole;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

// User/Assistant only -- System-role messages never reach this function (hoisted into
// systemInstruction before encodeHistory() is called).
QString roleToGoogleString(MessageRole role) {
  switch (role) {
    case MessageRole::Assistant:
      return QStringLiteral("model");
    case MessageRole::User:
    case MessageRole::System:
      return QStringLiteral("user");
  }
  return QStringLiteral("user");
}

QJsonObject functionCallPart(const ToolCallEntry& entry) {
  QJsonObject functionCall{
      {QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
      {QStringLiteral("args"), entry.input}};
  if (!entry.provider_call_id_synthesized && !entry.tool_use_id.isEmpty()) {
    functionCall[QStringLiteral("id")] = entry.tool_use_id;
  }
  QJsonObject part{{QStringLiteral("functionCall"), functionCall}};
  if (entry.thought_signature.has_value()) {
    part[QStringLiteral("thoughtSignature")] = *entry.thought_signature;  // sibling of "functionCall"
  }
  return part;
}

QJsonArray toolCallParts(const std::vector<ToolCallEntry>& entries) {
  QJsonArray parts;
  for (const auto& entry : entries) {
    parts.append(entry.kind == ToolCallKind::Invocation ? functionCallPart(entry)
                                                        : GoogleToolCodec::encodeFunctionResponse(entry));
  }
  return parts;
}

// Groups consecutive same-role Messages into one Gemini Content entry with a multi-Part array --
// mirrors AnthropicToolCodec::encodeHistory()'s MessageGroupBuilder, adapted to Gemini's
// {role, parts} shape.
class ContentGroupBuilder {
 public:
  void add(const QString& role, const QJsonArray& parts) {
    if (!has_current_ || role != last_role_) {
      flush();
      last_role_ = role;
      has_current_ = true;
      current_parts_ = parts;
      return;
    }
    for (const auto& part : parts) {
      current_parts_.append(part);
    }
  }

  QJsonArray finish() {
    flush();
    return contents_;
  }

 private:
  void flush() {
    if (!has_current_) {
      return;
    }
    contents_.append(QJsonObject{{QStringLiteral("role"), last_role_}, {QStringLiteral("parts"), current_parts_}});
    has_current_ = false;
  }

  QJsonArray contents_;
  QString last_role_;
  bool has_current_ = false;
  QJsonArray current_parts_;
};

}  // namespace

QJsonArray GoogleToolCodec::encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog) {
  if (catalog.client_tools.empty()) {
    return {};
  }
  QJsonArray declarations;
  for (const auto& definition : catalog.client_tools) {
    declarations.append(QJsonObject{{QStringLiteral("name"), definition.function_name},
                                    {QStringLiteral("description"), definition.description},
                                    {QStringLiteral("parameters"), definition.input_schema}});
  }
  return QJsonArray{QJsonObject{{QStringLiteral("functionDeclarations"), declarations}}};
}

QJsonArray GoogleToolCodec::encodeHistory(const std::vector<Message>& history, QStringList& system_parts) {
  ContentGroupBuilder builder;
  for (const Message& message : history) {
    if (message.role() == MessageRole::System) {
      if (!message.text().isEmpty()) {
        system_parts << message.text();
      }
      continue;
    }
    if (!message.toolCalls().empty()) {
      const bool isInvocation = message.toolCalls().front().kind == ToolCallKind::Invocation;
      builder.add(isInvocation ? QStringLiteral("model") : QStringLiteral("user"), toolCallParts(message.toolCalls()));
    } else if (!message.text().isEmpty()) {
      builder.add(roleToGoogleString(message.role()),
                  QJsonArray{QJsonObject{{QStringLiteral("text"), message.text()}}});
    }
  }
  return builder.finish();
}

std::optional<holonight_domain::ToolRequestEvent> GoogleToolCodec::decodeRequest(QString provider_instance_id,
                                                                                 const QJsonObject& part) {
  const QJsonObject functionCall = part.value(QStringLiteral("functionCall")).toObject();
  const QString name = functionCall.value(QStringLiteral("name")).toString();
  if (name.isEmpty()) {
    return std::nullopt;
  }

  QJsonObject args;
  if (functionCall.contains(QStringLiteral("args"))) {
    const QJsonValue argsValue = functionCall.value(QStringLiteral("args"));
    if (!argsValue.isObject()) {
      return std::nullopt;
    }
    args = argsValue.toObject();
  }

  QString providerCallId;
  bool synthesized = false;
  if (functionCall.contains(QStringLiteral("id"))) {
    providerCallId = functionCall.value(QStringLiteral("id")).toString();
  } else {
    providerCallId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    synthesized = true;
  }

  std::optional<QString> thoughtSignature;
  if (part.contains(QStringLiteral("thoughtSignature"))) {
    thoughtSignature = part.value(QStringLiteral("thoughtSignature")).toString();
  }

  return holonight_domain::ToolRequestEvent{
      .provider_call_id = providerCallId,
      .provider_instance_id = std::move(provider_instance_id),
      .function_name = name,
      .arguments = args,
      .execution_location = holonight_domain::ToolExecutionLocation::LocalClient,
      .source = holonight_domain::ToolSource::BuiltIn,
      .thought_signature = thoughtSignature,
      .provider_call_id_synthesized = synthesized,
  };
}

QJsonObject GoogleToolCodec::encodeFunctionResponse(const ToolCallEntry& entry) {
  QJsonObject functionResponse{
      {QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
      {QStringLiteral("response"), entry.result}};
  if (!entry.provider_call_id_synthesized && !entry.tool_use_id.isEmpty()) {
    functionResponse[QStringLiteral("id")] = entry.tool_use_id;
  }
  return QJsonObject{{QStringLiteral("functionResponse"), functionResponse}};
}

}  // namespace holonight_providers
