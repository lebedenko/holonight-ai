#include "holonight_providers/anthropic_tool_codec.h"

#include <QJsonDocument>

namespace holonight_providers {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageRole;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

QString roleToAnthropicString(MessageRole role) {
  return role == MessageRole::Assistant ? QStringLiteral("assistant") : QStringLiteral("user");
}

QJsonObject textContentBlock(const QString& text) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
}

QJsonObject invocationBlock(const ToolCallEntry& entry) {
  return QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                     {QStringLiteral("id"), entry.tool_use_id},
                     {QStringLiteral("name"), entry.function_name.isEmpty() ? entry.tool_name : entry.function_name},
                     {QStringLiteral("input"), entry.input}};
}

QJsonObject toolBlock(const ToolCallEntry& entry) {
  return entry.kind == ToolCallKind::Invocation ? invocationBlock(entry) : AnthropicToolCodec::encodeLocalResult(entry);
}

class MessageGroupBuilder {
 public:
  void add(const QString& role, const Message& message) {
    const bool isToolMessage = !message.toolCalls().empty();
    if (!has_current_ || role != last_role_) {
      flush();
      last_role_ = role;
      has_current_ = true;
      current_entry_ = QJsonObject{{QStringLiteral("role"), role}};
      if (isToolMessage) {
        blocks_ = QJsonArray{toolBlock(message.toolCalls().front())};
        array_form_ = true;
      } else {
        current_entry_[QStringLiteral("content")] = message.text();
        array_form_ = false;
      }
      return;
    }
    if (!array_form_) {
      blocks_ = QJsonArray{textContentBlock(current_entry_.value(QStringLiteral("content")).toString())};
      array_form_ = true;
    }
    blocks_.append(isToolMessage ? toolBlock(message.toolCalls().front()) : textContentBlock(message.text()));
  }

  QJsonArray finish() {
    flush();
    return messages_;
  }

 private:
  void flush() {
    if (!has_current_) {
      return;
    }
    if (array_form_) {
      current_entry_[QStringLiteral("content")] = blocks_;
    }
    messages_.append(current_entry_);
    has_current_ = false;
  }

  QJsonArray messages_;
  QString last_role_;
  bool has_current_ = false;
  QJsonObject current_entry_;
  QJsonArray blocks_;
  bool array_form_ = false;
};

}  // namespace

QJsonArray AnthropicToolCodec::encodeDefinitions(const holonight_domain::ToolCatalogSnapshot& catalog) {
  QJsonArray encoded;
  for (const auto& definition : catalog.client_tools) {
    encoded.append(QJsonObject{{QStringLiteral("name"), definition.function_name},
                               {QStringLiteral("description"), definition.description},
                               {QStringLiteral("input_schema"), definition.input_schema}});
  }
  return encoded;
}

QJsonArray AnthropicToolCodec::encodeHistory(const std::vector<Message>& history, QStringList& system_parts) {
  MessageGroupBuilder builder;
  for (const Message& message : history) {
    if (message.role() == MessageRole::System) {
      if (!message.text().isEmpty()) {
        system_parts << message.text();
      }
    } else if (!message.toolCalls().empty() || !message.text().isEmpty()) {
      builder.add(roleToAnthropicString(message.role()), message);
    }
  }
  return builder.finish();
}

QJsonObject AnthropicToolCodec::encodeLocalResult(const ToolCallEntry& entry) {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("tool_result")},
      {QStringLiteral("tool_use_id"), entry.tool_use_id},
      {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(entry.result).toJson(QJsonDocument::Compact))},
      {QStringLiteral("is_error"), entry.is_error},
  };
}

holonight_domain::ToolRequestEvent AnthropicToolCodec::decodeRequest(QString provider_instance_id,
                                                                     QString provider_call_id, QString function_name,
                                                                     QJsonObject arguments, bool provider_hosted) {
  return holonight_domain::ToolRequestEvent{
      .provider_call_id = std::move(provider_call_id),
      .provider_instance_id = std::move(provider_instance_id),
      .function_name = std::move(function_name),
      .arguments = std::move(arguments),
      .execution_location = provider_hosted ? holonight_domain::ToolExecutionLocation::ProviderHosted
                                            : holonight_domain::ToolExecutionLocation::LocalClient,
      .source = provider_hosted ? holonight_domain::ToolSource::Provider : holonight_domain::ToolSource::BuiltIn,
  };
}

}  // namespace holonight_providers
