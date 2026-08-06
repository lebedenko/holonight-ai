#include "holonight_rendering/message_content_parser.h"

#include "holonight_rendering/language_alias.h"

#include <QByteArray>
#include <QStringView>

#include <algorithm>
#include <md4c.h>
#include <optional>

namespace holonight_rendering {

namespace {

// Accumulates parse state across MD4C's callbacks. Offsets recovered from MD4C are interpreted
// against this UTF-8 byte array; see DESIGN.md §4.3 for the full parsing-strategy rationale.
struct ParseContext {
  QByteArray bytes;

  qsizetype last_flush_offset = 0;
  std::vector<ContentBlock> blocks;

  bool in_fenced_code_block = false;
  QByteArray code_accumulator;
  QString pending_language;
  char fence_char = 0;
  qsizetype fence_length = 0;
  qsizetype opening_fence_line_end = 0;
  qsizetype current_block_start = 0;
  bool terminal = true;

  [[nodiscard]] QString blockId(QStringView type, qsizetype byte_offset) const {
    const qsizetype source_offset = QString::fromUtf8(bytes.first(byte_offset)).size();
    return QStringLiteral("%1:%2").arg(type).arg(source_offset);
  }

  void flushMarkdown(qsizetype end_offset, bool complete) {
    end_offset = std::clamp(end_offset, last_flush_offset, bytes.size());
    const qsizetype length = end_offset - last_flush_offset;
    if (length > 0) {
      QString text = QString::fromUtf8(bytes.sliced(last_flush_offset, length));
      text = trimBlankBoundaryLines(text);
      if (!text.isEmpty()) {
        blocks.push_back(ContentBlock::markdown(text, blockId(u"markdown", last_flush_offset), complete));
      }
    }
    last_flush_offset = end_offset;
  }

 private:
  static QString trimBlankBoundaryLines(const QString& text) {
    qsizetype start = 0;
    qsizetype end = text.size();
    const QStringView view{text};

    while (start < end) {
      const qsizetype newline = text.indexOf(u'\n', start);
      const qsizetype line_end = newline < 0 ? end : newline;
      if (!view.sliced(start, line_end - start).trimmed().isEmpty()) {
        break;
      }
      start = newline < 0 ? end : newline + 1;
    }

    while (end > start) {
      const qsizetype newline = text.lastIndexOf(u'\n', end - 1);
      const qsizetype line_start = newline < start ? start : newline + 1;
      if (!view.sliced(line_start, end - line_start).trimmed().isEmpty()) {
        break;
      }
      end = newline < start ? start : newline;
    }

    return text.sliced(start, end - start);
  }
};

struct FenceLine {
  qsizetype start = 0;
  qsizetype end = 0;
  qsizetype length = 0;
};

qsizetype skipContainerPrefix(const ParseContext& ctx, qsizetype cursor, qsizetype line_end) {
  while (cursor < line_end) {
    const qsizetype prefix_start = cursor;
    int spaces = 0;
    while (cursor < line_end && ctx.bytes.at(cursor) == ' ' && spaces < 3) {
      ++cursor;
      ++spaces;
    }
    if (cursor >= line_end || ctx.bytes.at(cursor) != '>') {
      return prefix_start;
    }
    ++cursor;
    if (cursor < line_end && ctx.bytes.at(cursor) == ' ') {
      ++cursor;
    }
  }
  return cursor;
}

std::optional<FenceLine> findFenceLine(const ParseContext& ctx, qsizetype from_offset, char fence_char,
                                       qsizetype minimum_length, bool closing) {
  qsizetype line_start = from_offset;
  while (line_start < ctx.bytes.size()) {
    qsizetype line_end = line_start;
    while (line_end < ctx.bytes.size() && ctx.bytes.at(line_end) != '\n') {
      ++line_end;
    }

    qsizetype cursor = skipContainerPrefix(ctx, line_start, line_end);
    int spaces = 0;
    while (cursor < line_end && ctx.bytes.at(cursor) == ' ' && spaces < 3) {
      ++cursor;
      ++spaces;
    }

    const qsizetype fence_start = cursor;
    while (cursor < line_end && ctx.bytes.at(cursor) == fence_char) {
      ++cursor;
    }
    const qsizetype fence_length = cursor - fence_start;
    bool remainder_is_whitespace = true;
    for (qsizetype i = cursor; i < line_end; ++i) {
      if (ctx.bytes.at(i) != ' ' && ctx.bytes.at(i) != '\t' && ctx.bytes.at(i) != '\r') {
        remainder_is_whitespace = false;
        break;
      }
    }

    if (fence_length >= minimum_length && (!closing || remainder_is_whitespace)) {
      return FenceLine{
          .start = line_start,
          .end = line_end < ctx.bytes.size() ? line_end + 1 : line_end,
          .length = fence_length,
      };
    }
    line_start = line_end < ctx.bytes.size() ? line_end + 1 : ctx.bytes.size();
  }
  return std::nullopt;
}

int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* ctx = static_cast<ParseContext*>(userdata);
  if (type == MD_BLOCK_CODE) {
    const auto* code_detail = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
    if (code_detail->fence_char != 0) {  // fenced (vs. indented, which stays in the Markdown flow)
      const auto opening_fence = findFenceLine(*ctx, ctx->last_flush_offset, code_detail->fence_char, 3, false);
      if (!opening_fence.has_value()) {
        return 1;
      }
      ctx->flushMarkdown(opening_fence->start, true);
      ctx->in_fenced_code_block = true;
      ctx->code_accumulator.clear();
      ctx->pending_language = QString::fromUtf8(code_detail->lang.text, static_cast<qsizetype>(code_detail->lang.size));
      ctx->fence_char = code_detail->fence_char;
      ctx->fence_length = opening_fence->length;
      ctx->opening_fence_line_end = opening_fence->end;
      ctx->current_block_start = opening_fence->start;
    }
  }
  return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
  auto* ctx = static_cast<ParseContext*>(userdata);
  if (type == MD_BLOCK_CODE && ctx->in_fenced_code_block) {
    QString code = QString::fromUtf8(ctx->code_accumulator);
    const QString language = LanguageAlias::normalize(ctx->pending_language);
    const auto closing_fence =
        findFenceLine(*ctx, ctx->opening_fence_line_end, ctx->fence_char, ctx->fence_length, true);
    if (!closing_fence.has_value()) {
      code = QString::fromUtf8(ctx->bytes.sliced(ctx->opening_fence_line_end));
      ctx->blocks.push_back(
          ContentBlock::code(code, language, ctx->blockId(u"code", ctx->current_block_start), ctx->terminal));
      ctx->last_flush_offset = ctx->bytes.size();
      ctx->in_fenced_code_block = false;
      return 0;
    }
    ctx->blocks.push_back(ContentBlock::code(code, language, ctx->blockId(u"code", ctx->current_block_start), true));
    ctx->last_flush_offset = closing_fence->end;
    ctx->in_fenced_code_block = false;
  }
  return 0;
}

// MD4C calls enter_span/leave_span unconditionally for every span-level construct (emphasis,
// inline code, links, ...) -- unlike debug_log, they are NOT documented as optional/nullable in
// md4c.h, and leaving them null segfaults on any message containing inline markdown spans. We
// don't need span-level detail (blocks are flushed as verbatim Markdown text and rendered via
// Text.MarkdownText), so these are no-ops that just let parsing continue.
int enterSpan(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/) { return 0; }
int leaveSpan(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/) { return 0; }

int text(MD_TEXTTYPE type, const MD_CHAR* text_ptr, MD_SIZE size, void* userdata) {
  auto* ctx = static_cast<ParseContext*>(userdata);
  if (ctx->in_fenced_code_block && type == MD_TEXT_CODE) {
    ctx->code_accumulator.append(text_ptr, static_cast<qsizetype>(size));
  }
  return 0;
}

}  // namespace

std::vector<ContentBlock> MessageContentParser::parse(const QString& markdownText, bool terminal) {
  if (markdownText.isEmpty()) {
    return {};  // REQ-F-002: empty message -> empty list, not a single empty block
  }

  const QByteArray utf8 = markdownText.toUtf8();

  ParseContext ctx;
  ctx.bytes = utf8;
  ctx.terminal = terminal;

  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = 0;
  parser.enter_block = &enterBlock;
  parser.leave_block = &leaveBlock;
  parser.enter_span = &enterSpan;
  parser.leave_span = &leaveSpan;
  parser.text = &text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  const int result = md_parse(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), &parser, &ctx);
  if (result != 0) {
    // REQ-F-002: parsing errors never crash and never lose content — fall back to the original
    // text verbatim as a single Markdown block.
    return {ContentBlock::markdown(markdownText, QStringLiteral("markdown:0"), terminal)};
  }

  ctx.flushMarkdown(ctx.bytes.size(), terminal);
  if (!terminal && !ctx.blocks.empty()) {
    ContentBlock& tail = ctx.blocks.back();
    if (tail.complete() && tail.type() == ContentBlockType::Markdown) {
      tail = ContentBlock::markdown(tail.text(), tail.id(), false);
    }
  }
  return ctx.blocks;
}

}  // namespace holonight_rendering
