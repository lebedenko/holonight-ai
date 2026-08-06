#include "holonight_providers/openai_tool_codec.h"

#include <QJsonDocument>

#include <gtest/gtest.h>

namespace holonight_providers {
namespace {

TEST(OpenAIToolCodec, EncodesResponsesFunctionDefinitionsWithoutStrictMode) {
  const holonight_domain::ToolCatalogSnapshot catalog{
      .client_tools = {{
          .function_name = QStringLiteral("list_files"),
          .description = QStringLiteral("List files"),
          .input_schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}},
      }}};

  const QJsonArray definitions = OpenAIToolCodec::encodeDefinitions(catalog);

  ASSERT_EQ(definitions.size(), 1);
  const QJsonObject definition = definitions.at(0).toObject();
  EXPECT_EQ(definition.value(QStringLiteral("type")), QStringLiteral("function"));
  EXPECT_EQ(definition.value(QStringLiteral("name")), QStringLiteral("list_files"));
  EXPECT_TRUE(definition.value(QStringLiteral("parameters")).isObject());
  EXPECT_FALSE(definition.contains(QStringLiteral("strict")));
}

TEST(OpenAIToolCodec, DecodesMultipleCallsInOrderWithOpaqueReasoning) {
  const QJsonObject reasoning{{QStringLiteral("type"), QStringLiteral("reasoning")},
                              {QStringLiteral("id"), QStringLiteral("rs_1")},
                              {QStringLiteral("encrypted_content"), QStringLiteral("opaque")}};
  const QJsonArray output{
      reasoning,
      QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                  {QStringLiteral("id"), QStringLiteral("fc_1")},
                  {QStringLiteral("call_id"), QStringLiteral("call_1")},
                  {QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QStringLiteral(R"({"path":"a"})")}},
      QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                  {QStringLiteral("call_id"), QStringLiteral("call_2")},
                  {QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QStringLiteral(R"({"path":"b"})")}},
  };

  const auto decoded = OpenAIToolCodec::decodeRequests(QStringLiteral("work"), output);

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 2U);
  EXPECT_EQ(decoded->at(0).provider_call_id, QStringLiteral("call_1"));
  EXPECT_EQ(decoded->at(1).arguments.value(QStringLiteral("path")), QStringLiteral("b"));
  EXPECT_EQ(decoded->at(0).provider_item_id, QStringLiteral("fc_1"));
  EXPECT_EQ(decoded->at(0).provider_context, std::vector<QJsonObject>{reasoning});
}

TEST(OpenAIToolCodec, RejectsMalformedArgumentsWithoutReturningPartialCalls) {
  const QJsonArray output{
      QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                  {QStringLiteral("call_id"), QStringLiteral("ok")},
                  {QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QStringLiteral("{}")}},
      QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                  {QStringLiteral("call_id"), QStringLiteral("bad")},
                  {QStringLiteral("name"), QStringLiteral("list_files")},
                  {QStringLiteral("arguments"), QStringLiteral("[]")}},
  };

  EXPECT_FALSE(OpenAIToolCodec::decodeRequests(QStringLiteral("work"), output).has_value());
}

TEST(OpenAIToolCodec, ReconstructsCallsOutputsAndDeduplicatesReasoningById) {
  const QJsonObject reasoning{{QStringLiteral("type"), QStringLiteral("reasoning")},
                              {QStringLiteral("id"), QStringLiteral("rs_1")},
                              {QStringLiteral("encrypted_content"), QStringLiteral("opaque")}};
  holonight_domain::Message invocation;
  invocation.setToolCalls({holonight_domain::ToolCallEntry{
      .kind = holonight_domain::ToolCallKind::Invocation,
      .tool_use_id = QStringLiteral("call_1"),
      .tool_name = QStringLiteral("list_files"),
      .input = QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}},
      .provider_item_id = QStringLiteral("fc_1"),
      .provider_context = {reasoning},
  }});
  holonight_domain::Message result;
  result.setToolCalls({holonight_domain::ToolCallEntry{
      .kind = holonight_domain::ToolCallKind::Result,
      .tool_use_id = QStringLiteral("call_1"),
      .result = QJsonObject{{QStringLiteral("files"), QJsonArray{QStringLiteral("a")}}},
      .provider_context = {reasoning},
  }});

  const QJsonArray history = OpenAIToolCodec::encodeHistory({invocation, result});

  ASSERT_EQ(history.size(), 3);
  EXPECT_EQ(history.at(0).toObject(), reasoning);
  EXPECT_EQ(history.at(1).toObject().value(QStringLiteral("type")), QStringLiteral("function_call"));
  EXPECT_EQ(history.at(2).toObject().value(QStringLiteral("output")), QStringLiteral(R"({"files":["a"]})"));
}

}  // namespace
}  // namespace holonight_providers
