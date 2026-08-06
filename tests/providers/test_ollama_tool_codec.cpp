#include "holonight_providers/ollama_tool_codec.h"

#include <QJsonDocument>
#include <QUuid>

#include <gtest/gtest.h>

namespace holonight_providers {
namespace {

using holonight_domain::Message;
using holonight_domain::MessageId;
using holonight_domain::MessageRole;
using holonight_domain::ToolCallEntry;
using holonight_domain::ToolCallKind;

TEST(OllamaToolCodec, EncodesToolDefinitionsAsFunctionObjects) {
  const holonight_domain::ToolCatalogSnapshot catalog{
      .client_tools = {{
          .function_name = QStringLiteral("list_files"),
          .description = QStringLiteral("List files"),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};

  const QJsonArray definitions = OllamaToolCodec::encodeDefinitions(catalog);

  ASSERT_EQ(definitions.size(), 1);
  const QJsonObject entry = definitions.at(0).toObject();
  EXPECT_EQ(entry.value(QStringLiteral("type")), QStringLiteral("function"));
  const QJsonObject function = entry.value(QStringLiteral("function")).toObject();
  EXPECT_EQ(function.value(QStringLiteral("name")), QStringLiteral("list_files"));
  EXPECT_EQ(function.value(QStringLiteral("description")), QStringLiteral("List files"));
  EXPECT_TRUE(function.value(QStringLiteral("parameters")).isObject());
}

TEST(OllamaToolCodec, EncodeDefinitionsReturnsEmptyArrayForEmptyCatalog) {
  EXPECT_TRUE(OllamaToolCodec::encodeDefinitions({}).isEmpty());
}

TEST(OllamaToolCodec, DecodeRequestsUsesProvidedIdVerbatim) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("id"), QStringLiteral("call_xyz")},
      {QStringLiteral("function"),
       QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                   {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}}}}};

  const auto decoded = OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 1U);
  EXPECT_EQ(decoded->at(0).provider_call_id, QStringLiteral("call_xyz"));
  EXPECT_FALSE(decoded->at(0).provider_call_id_synthesized);
  EXPECT_EQ(decoded->at(0).provider_instance_id, QStringLiteral("ollama-1"));
  EXPECT_EQ(decoded->at(0).function_name, QStringLiteral("list_files"));
  EXPECT_EQ(decoded->at(0).arguments.value(QStringLiteral("path")), QStringLiteral("~"));
}

TEST(OllamaToolCodec, DecodeRequestsSynthesizesUuidWhenIdAbsent) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("function"),
       QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                   {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}}}}};

  const auto decoded = OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 1U);
  EXPECT_TRUE(decoded->at(0).provider_call_id_synthesized);
  EXPECT_FALSE(QUuid::fromString(decoded->at(0).provider_call_id).isNull());
}

TEST(OllamaToolCodec, DecodeRequestsEmitsMultipleEventsInArrayOrder) {
  const QJsonArray toolCalls{
      QJsonObject{
          {QStringLiteral("function"),
           QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                       {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("a")}}}}}},
      QJsonObject{
          {QStringLiteral("function"),
           QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                       {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("b")}}}}}},
  };

  const auto decoded = OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 2U);
  EXPECT_EQ(decoded->at(0).arguments.value(QStringLiteral("path")), QStringLiteral("a"));
  EXPECT_EQ(decoded->at(1).arguments.value(QStringLiteral("path")), QStringLiteral("b"));
}

TEST(OllamaToolCodec, DecodeRequestsFailsOnMissingFunctionName) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("function"),
       QJsonObject{{QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}}}}}}};

  EXPECT_FALSE(OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls).has_value());
}

TEST(OllamaToolCodec, DecodeRequestsFailsWhenArgumentsIsJsonString) {
  const QJsonArray toolCalls{QJsonObject{
      {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                                               {QStringLiteral("arguments"), QStringLiteral("not an object")}}}}};

  EXPECT_FALSE(OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls).has_value());
}

TEST(OllamaToolCodec, DecodeRequestsFailsWhenArgumentsIsMissing) {
  const QJsonArray toolCalls{
      QJsonObject{{QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")}}}}};

  EXPECT_FALSE(OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls).has_value());
}

TEST(OllamaToolCodec, DecodeRequestsRejectsAllCallsWhenAnyEntryMalformed) {
  const QJsonArray toolCalls{
      QJsonObject{
          {QStringLiteral("function"),
           QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                       {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("path"), QStringLiteral("a")}}}}}},
      QJsonObject{
          {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("list_files")},
                                                   {QStringLiteral("arguments"), QStringLiteral("not an object")}}}},
  };

  EXPECT_FALSE(OllamaToolCodec::decodeRequests(QStringLiteral("ollama-1"), toolCalls).has_value());
}

TEST(OllamaToolCodec, EncodeHistoryReconstructsToolCallAndResultAsSeparateMessages) {
  const Message invocation(MessageId::generate(), MessageRole::Assistant, QString(),
                           holonight_domain::MessageStatus::Complete, QDateTime(), std::nullopt,
                           std::vector<ToolCallEntry>{ToolCallEntry{
                               .kind = ToolCallKind::Invocation,
                               .tool_use_id = QStringLiteral("call_1"),
                               .function_name = QStringLiteral("list_files"),
                               .input = QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}},
                           }});
  const Message result(MessageId::generate(), MessageRole::User, QString(), holonight_domain::MessageStatus::Complete,
                       QDateTime(), std::nullopt,
                       std::vector<ToolCallEntry>{ToolCallEntry{
                           .kind = ToolCallKind::Result,
                           .tool_use_id = QStringLiteral("call_1"),
                           .function_name = QStringLiteral("list_files"),
                           .result = QJsonObject{{QStringLiteral("files"), QJsonArray{QStringLiteral("a")}}},
                       }});

  const QJsonArray history = OllamaToolCodec::encodeHistory({invocation, result});

  ASSERT_EQ(history.size(), 2);
  const QJsonObject assistantMessage = history.at(0).toObject();
  EXPECT_EQ(assistantMessage.value(QStringLiteral("role")), QStringLiteral("assistant"));
  ASSERT_TRUE(assistantMessage.value(QStringLiteral("tool_calls")).isArray());
  const QJsonArray toolCalls = assistantMessage.value(QStringLiteral("tool_calls")).toArray();
  ASSERT_EQ(toolCalls.size(), 1);
  EXPECT_EQ(toolCalls.at(0).toObject().value(QStringLiteral("function")).toObject().value(QStringLiteral("name")),
            QStringLiteral("list_files"));

  const QJsonObject toolMessage = history.at(1).toObject();
  EXPECT_EQ(toolMessage.value(QStringLiteral("role")), QStringLiteral("tool"));
  EXPECT_EQ(toolMessage.value(QStringLiteral("tool_name")), QStringLiteral("list_files"));
  EXPECT_EQ(toolMessage.value(QStringLiteral("content")), QStringLiteral(R"({"files":["a"]})"));
}

TEST(OllamaToolCodec, EncodeHistoryGroupsConsecutiveAssistantTextAndInvocationIntoOneMessage) {
  const Message text(MessageId::generate(), MessageRole::Assistant, QStringLiteral("Let me check."));
  const Message invocation(MessageId::generate(), MessageRole::Assistant, QString(),
                           holonight_domain::MessageStatus::Complete, QDateTime(), std::nullopt,
                           std::vector<ToolCallEntry>{ToolCallEntry{
                               .kind = ToolCallKind::Invocation,
                               .tool_use_id = QStringLiteral("call_1"),
                               .function_name = QStringLiteral("list_files"),
                               .input = QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}},
                           }});

  const QJsonArray history = OllamaToolCodec::encodeHistory({text, invocation});

  ASSERT_EQ(history.size(), 1);
  const QJsonObject message = history.at(0).toObject();
  EXPECT_EQ(message.value(QStringLiteral("role")), QStringLiteral("assistant"));
  EXPECT_EQ(message.value(QStringLiteral("content")), QStringLiteral("Let me check."));
  ASSERT_TRUE(message.value(QStringLiteral("tool_calls")).isArray());
  EXPECT_EQ(message.value(QStringLiteral("tool_calls")).toArray().size(), 1);
}

TEST(OllamaToolCodec, EncodeHistoryDoesNotMergeConsecutiveResultMessages) {
  const Message resultA(MessageId::generate(), MessageRole::User, QString(), holonight_domain::MessageStatus::Complete,
                        QDateTime(), std::nullopt,
                        std::vector<ToolCallEntry>{ToolCallEntry{
                            .kind = ToolCallKind::Result,
                            .tool_use_id = QStringLiteral("call_1"),
                            .function_name = QStringLiteral("list_files"),
                            .result = QJsonObject{{QStringLiteral("files"), QJsonArray{QStringLiteral("a")}}},
                        }});
  const Message resultB(MessageId::generate(), MessageRole::User, QString(), holonight_domain::MessageStatus::Complete,
                        QDateTime(), std::nullopt,
                        std::vector<ToolCallEntry>{ToolCallEntry{
                            .kind = ToolCallKind::Result,
                            .tool_use_id = QStringLiteral("call_2"),
                            .function_name = QStringLiteral("list_files"),
                            .result = QJsonObject{{QStringLiteral("files"), QJsonArray{QStringLiteral("b")}}},
                        }});

  const QJsonArray history = OllamaToolCodec::encodeHistory({resultA, resultB});

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history.at(0).toObject().value(QStringLiteral("role")), QStringLiteral("tool"));
  EXPECT_EQ(history.at(1).toObject().value(QStringLiteral("role")), QStringLiteral("tool"));
  EXPECT_EQ(history.at(0).toObject().value(QStringLiteral("content")), QStringLiteral(R"({"files":["a"]})"));
  EXPECT_EQ(history.at(1).toObject().value(QStringLiteral("content")), QStringLiteral(R"({"files":["b"]})"));
}

}  // namespace
}  // namespace holonight_providers
