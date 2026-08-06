#pragma once

#include <QString>

#include <holonight_rendering/content_block.h>
#include <vector>

namespace holonight_rendering {

// Stateless, synchronous, MD4C-backed. Safe to default-construct per call site (MessageListModel
// keeps one instance as a member; there is no per-parse setup expensive enough to amortize
// further).
class MessageContentParser {
 public:
  // Empty input -> empty vector (REQ-F-002: "an empty message produces an empty block list, not a
  // single empty block"). Malformed input is handled internally by falling back to a single
  // Markdown block containing the original text verbatim (REQ-F-002: parsing errors never crash
  // the app and never lose the message content).
  [[nodiscard]] static std::vector<ContentBlock> parse(const QString& markdownText, bool terminal = true);
};

}  // namespace holonight_rendering
