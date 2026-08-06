#pragma once

#include "holonight_application/tools/tool_contracts.h"

#include <QDateTime>

#include <functional>
#include <holonight_domain/tool_activity.h>
#include <holonight_domain/tool_call.h>

namespace holonight_application {

class ToolRegistry;

class ToolOrchestrator {
 public:
  struct Callbacks {
    std::function<void(const holonight_domain::ToolInvocation&)> on_running;
    std::function<void(const holonight_domain::ToolInvocation&)> on_capabilities_changed;
    std::function<void(const holonight_domain::ToolInvocation&)> on_terminal;
  };

  struct ExecutionRequest {
    QString invocation_id;
    QString provider_call_id;
    QString function_name;
    QJsonValue arguments;
    QDateTime requested_at;
  };

  [[nodiscard]] static ToolExecutionHandlePtr execute(const ToolRegistry& registry, const ExecutionRequest& request,
                                                      const Callbacks& callbacks);
};

}  // namespace holonight_application
