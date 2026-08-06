#pragma once

#include "holonight_application/tools/i_tool.h"
#include "holonight_application/tools/tool_contracts.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

namespace holonight_application {

// REQ-F-006. Named to match SPEC.md's acceptance-criteria phrase ("MAX_TOOL_CALLS_PER_TURN") for
// grep-ability, spelled per this codebase's own constant convention (see kModelDiscoveryTimeout
// in anthropic_provider.cpp) -- there is no second, ALL_CAPS symbol for the same constant.
inline constexpr int kMaxToolCallsPerTurn = 10;

// Owns tool lifetime/lookup and provides generic, name-based dispatch so orchestration code never
// branches on individual tool identities (REQ-F-001).
class ToolRegistry {
 public:
  // Tools are registered once at startup (REQ-NF-003) -- not a runtime hot path. Registering a
  // duplicate name is a programming error, caught by a debug assertion rather than silently
  // letting the later registration win.
  void registerTool(std::shared_ptr<ITool> tool);
  void registerTool(ToolRegistration registration);

  // REQ-F-005. All built-in/local registrations must include definition + executor + presenter,
  // and aliases resolve via function name lookups.
  [[nodiscard]] const ToolRegistration* registrationByFunctionName(const QString& name) const;

  [[nodiscard]] holonight_domain::ToolCatalogSnapshot catalogSnapshot() const;

  // Registration order (REQ-NF-003).
  [[nodiscard]] const std::vector<std::shared_ptr<ITool>>& tools() const;

  [[nodiscard]] std::shared_ptr<ITool> find(const QString& name) const;  // nullptr if unknown

  // Generic name-based dispatch (REQ-F-001): the only call site orchestration code needs. If
  // `name` is not registered, returns a structured error object rather than crashing.
  [[nodiscard]] QJsonObject invoke(const QString& name, const QJsonObject& parameters) const;

 private:
  struct RegisteredTool {
    ToolRegistration registration;
    std::shared_ptr<ITool> legacy_tool;
  };

  void registerToolInternal(ToolRegistration registration, std::shared_ptr<ITool> legacy_tool);
  [[nodiscard]] const RegisteredTool* findRecord(const QString& name) const;

  std::vector<RegisteredTool> registrations_;
  mutable std::vector<std::shared_ptr<ITool>> tools_;
  QHash<QString, std::size_t> by_function_name_;
  QHash<QString, std::size_t> by_canonical_id_;
  QHash<QString, std::size_t> by_renderer_key_;
  QHash<QString, std::size_t> by_alias_;
};

}  // namespace holonight_application
