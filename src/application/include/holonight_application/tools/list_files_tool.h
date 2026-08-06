#pragma once

#include "holonight_application/tools/i_tool.h"
#include "holonight_application/tools/tool_contracts.h"

#include <filesystem>

namespace holonight_application {

// Lists the immediate (non-recursive) entries of a directory, restricted to paths that
// canonically resolve within the OS user's home directory (REQ-F-003, REQ-F-004, REQ-C-003,
// REQ-C-006).
class ListFilesTool : public ITool {
 public:
  ListFilesTool();

  // Test-only seam: restricts the tool to an explicit root instead of the real $HOME, so tests
  // can exercise boundary/error behavior hermetically without depending on the test-runner's
  // actual home directory contents. Production code must use the default constructor --
  // REQ-C-003 forbids exposing this as user- or conversation-configurable.
  explicit ListFilesTool(const std::filesystem::path& homeOverride);

  [[nodiscard]] QString name() const override;
  [[nodiscard]] QString description() const override;
  [[nodiscard]] QJsonObject schema() const override;
  [[nodiscard]] QJsonObject execute(const QJsonObject& parameters) override;

 private:
  // Canonicalized once at construction time (REQ-C-003: $HOME is hardcoded per-process, not
  // reconfigurable, so a single computation is correct, not a staleness risk).
  std::filesystem::path home_;
};

[[nodiscard]] ToolRegistration listFilesToolRegistration();

}  // namespace holonight_application
