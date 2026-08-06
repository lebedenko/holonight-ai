#include "holonight_rendering/message_content_parser.h"

#include <QString>

#include <gtest/gtest.h>

namespace holonight_rendering {
namespace {

TEST(MessageContentParser, EmptyInputProducesNoBlocks) { EXPECT_TRUE(MessageContentParser::parse(QString()).empty()); }

TEST(MessageContentParser, ExtractsEmptyFenceWithoutLeavingFenceMarkdown) {
  const std::vector<ContentBlock> blocks =
      MessageContentParser::parse(QStringLiteral("before\n\n```cpp\n```\n\nafter"));

  ASSERT_EQ(blocks.size(), 3U);
  EXPECT_EQ(blocks[0].text(), QStringLiteral("before"));
  EXPECT_EQ(blocks[1].text(), QString());
  EXPECT_EQ(blocks[1].language(), QStringLiteral("C++"));
  EXPECT_EQ(blocks[2].text(), QStringLiteral("after"));
  EXPECT_FALSE(blocks[0].id().isEmpty());
  EXPECT_FALSE(blocks[1].id().isEmpty());
}

TEST(MessageContentParser, PreservesSignificantWhitespaceAtMarkdownBoundaries) {
  const std::vector<ContentBlock> blocks =
      MessageContentParser::parse(QStringLiteral("    indented\nline with hard break  \n\n```text\ncode\n```"));

  ASSERT_EQ(blocks.size(), 2U);
  EXPECT_EQ(blocks[0].type(), ContentBlockType::Markdown);
  EXPECT_EQ(blocks[0].text(), QStringLiteral("    indented\nline with hard break  "));
  EXPECT_EQ(blocks[1].text(), QStringLiteral("code\n"));
  EXPECT_EQ(blocks[1].language(), QStringLiteral("text"));
}

TEST(MessageContentParser, ExtractsAdjacentBacktickAndTildeFencesInOrder) {
  const std::vector<ContentBlock> blocks =
      MessageContentParser::parse(QStringLiteral("```js\nconst a = 1;\n```\n~~~py\nprint('ok')\n~~~"));

  ASSERT_EQ(blocks.size(), 2U);
  EXPECT_EQ(blocks[0].text(), QStringLiteral("const a = 1;\n"));
  EXPECT_EQ(blocks[0].language(), QStringLiteral("JavaScript"));
  EXPECT_EQ(blocks[1].text(), QStringLiteral("print('ok')\n"));
  EXPECT_EQ(blocks[1].language(), QStringLiteral("Python"));
}

TEST(MessageContentParser, ExtractsBlockquotedFenceWithoutLosingUnicode) {
  const std::vector<ContentBlock> blocks =
      MessageContentParser::parse(QStringLiteral("Перед\n\n> ```qml\n> Text { text: \"ніч\" }\n> ```\n\nПісля"));

  ASSERT_EQ(blocks.size(), 3U);
  EXPECT_EQ(blocks[0].text(), QStringLiteral("Перед"));
  EXPECT_EQ(blocks[1].text(), QStringLiteral("Text { text: \"ніч\" }\n"));
  EXPECT_EQ(blocks[1].language(), QStringLiteral("QML"));
  EXPECT_EQ(blocks[2].text(), QStringLiteral("Після"));
}

TEST(MessageContentParser, LeavesIndentedCodeInMarkdown) {
  const std::vector<ContentBlock> blocks = MessageContentParser::parse(QStringLiteral("    int value = 1;\n"));

  ASSERT_EQ(blocks.size(), 1U);
  EXPECT_EQ(blocks[0].text(), QStringLiteral("    int value = 1;"));
}

TEST(MessageContentParser, StreamingTailIsIncompleteAndKeepsIdentityAtTerminal) {
  const auto streaming = MessageContentParser::parse(QStringLiteral("intro\n\n```cpp\nint value = 1;"), false);
  ASSERT_EQ(streaming.size(), 2U);
  EXPECT_TRUE(streaming[0].complete());
  EXPECT_FALSE(streaming[1].complete());
  EXPECT_EQ(streaming[1].text(), QStringLiteral("int value = 1;"));

  const auto terminal = MessageContentParser::parse(QStringLiteral("intro\n\n```cpp\nint value = 1;"), true);
  ASSERT_EQ(terminal.size(), 2U);
  EXPECT_EQ(terminal[1].id(), streaming[1].id());
  EXPECT_TRUE(terminal[1].complete());
}

TEST(MessageContentParser, ClosingFenceCompletesSameCodeBlock) {
  const auto open = MessageContentParser::parse(QStringLiteral("~~~py\nprint('ніч')\n"), false);
  const auto closed = MessageContentParser::parse(QStringLiteral("~~~py\nprint('ніч')\n~~~"), false);

  ASSERT_EQ(open.size(), 1U);
  ASSERT_EQ(closed.size(), 1U);
  EXPECT_EQ(open[0].id(), closed[0].id());
  EXPECT_FALSE(open[0].complete());
  EXPECT_TRUE(closed[0].complete());
  EXPECT_EQ(closed[0].text(), QStringLiteral("print('ніч')\n"));
}

}  // namespace
}  // namespace holonight_rendering
