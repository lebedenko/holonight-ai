#include "holonight_rendering/content_block.h"

namespace holonight_rendering {

ContentBlock ContentBlock::markdown(QString text, QString id, bool complete) {
  ContentBlock block;
  block.type_ = ContentBlockType::Markdown;
  block.id_ = std::move(id);
  block.complete_ = complete;
  block.payload_ = MarkdownPayload{.text = std::move(text)};
  return block;
}

ContentBlock ContentBlock::code(QString code, QString normalizedLanguage, QString id, bool complete) {
  ContentBlock block;
  block.type_ = ContentBlockType::Code;
  block.id_ = std::move(id);
  block.complete_ = complete;
  block.payload_ = CodePayload{.code = std::move(code), .language = std::move(normalizedLanguage)};
  return block;
}

QString ContentBlock::text() const {
  if (const auto* markdown = std::get_if<MarkdownPayload>(&payload_)) {
    return markdown->text;
  }
  return std::get<CodePayload>(payload_).code;
}

QString ContentBlock::language() const {
  if (const auto* code = std::get_if<CodePayload>(&payload_)) {
    return code->language;
  }
  return {};
}

}  // namespace holonight_rendering
