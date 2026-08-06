#pragma once

#include <QJsonObject>
#include <QString>

namespace holonight_application {

// Contract every tool the model can invoke must implement. ToolRegistry dispatches to
// implementations purely by name -- no code outside a concrete ITool implementation may know
// which tools exist (REQ-F-001).
class ITool {
 public:
  ITool() = default;
  virtual ~ITool() = default;
  ITool(const ITool&) = delete;
  ITool& operator=(const ITool&) = delete;
  ITool(ITool&&) = delete;
  ITool& operator=(ITool&&) = delete;

  // Stable, unique identifier sent to Anthropic as tools[].name and matched against tool_use
  // blocks' "name" field on the way back in. Must not change across releases once a tool ships.
  [[nodiscard]] virtual QString name() const = 0;

  // Sent as tools[].description -- the model's only signal for when to call this tool.
  [[nodiscard]] virtual QString description() const = 0;

  // JSON Schema v7 object describing the tool's input parameters (REQ-NF-001); becomes
  // tools[].input_schema verbatim.
  [[nodiscard]] virtual QJsonObject schema() const = 0;

  // Synchronous, never throws (REQ-F-005, REQ-NF-002). Implementations must catch every
  // exception/filesystem error internally and return a structured {"error": {...}} object instead
  // of letting anything escape -- ToolRegistry::invoke() does not wrap this call in a try/catch.
  [[nodiscard]] virtual QJsonObject execute(const QJsonObject& parameters) = 0;
};

}  // namespace holonight_application
