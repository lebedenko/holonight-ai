#pragma once

#include "holonight_domain/tool_activity.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace holonight_application {

struct ToolDefinition {
  QString id;             // Stable application identifier, e.g. "filesystem.list".
  QString function_name;  // Provider-safe invocation name, e.g. "list_files".
  QString display_name;   // User-facing label for semantic presenter text.
  QString renderer_key;   // Contract key for renderer registry, e.g. "filesystem.list".
  QString description;
  QJsonObject input_schema;

  enum class ToolRisk : std::uint8_t {
    Safe,
    PotentiallySensitive,
    Unsafe,
  };

  ToolRisk risk = ToolRisk::Safe;
};

struct ToolExecutionRequest {
  QString invocation_id;
  QString provider_call_id;
  QString function_name;
  QJsonValue parameters;
};
struct ToolOutcome {
  std::optional<holonight_domain::ToolError> error;
  QJsonValue result;
};

class ToolExecutionHandle {
 public:
  ToolExecutionHandle() = default;
  virtual ~ToolExecutionHandle() = default;
  ToolExecutionHandle(const ToolExecutionHandle&) = default;
  ToolExecutionHandle& operator=(const ToolExecutionHandle&) = default;
  ToolExecutionHandle(ToolExecutionHandle&&) = default;
  ToolExecutionHandle& operator=(ToolExecutionHandle&&) = default;
  [[nodiscard]] virtual bool canCancel() const = 0;
  virtual void cancel() = 0;
};

using ToolExecutionHandlePtr = std::shared_ptr<ToolExecutionHandle>;
using ToolOutcomeCallback = std::function<void(const ToolOutcome&)>;

class IToolExecutor {
 public:
  IToolExecutor() = default;
  virtual ~IToolExecutor() = default;
  IToolExecutor(const IToolExecutor&) = default;
  IToolExecutor& operator=(const IToolExecutor&) = default;
  IToolExecutor(IToolExecutor&&) = default;
  IToolExecutor& operator=(IToolExecutor&&) = default;
  [[nodiscard]] virtual ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                                     ToolOutcomeCallback on_finished) = 0;
};

struct ToolPresentation {
  QString renderer_key;
  QString icon_name;
  QString title;
  QString summary;
  QString status_text;
  QVariantMap detail_data;
  QString raw_arguments_json;
  QString raw_result_json;
  bool is_error = false;
  bool can_cancel = false;
};

class IToolPresenter {
 public:
  IToolPresenter() = default;
  virtual ~IToolPresenter() = default;
  IToolPresenter(const IToolPresenter&) = default;
  IToolPresenter& operator=(const IToolPresenter&) = default;
  IToolPresenter(IToolPresenter&&) = default;
  IToolPresenter& operator=(IToolPresenter&&) = default;
  [[nodiscard]] virtual ToolPresentation present(const holonight_domain::ToolInvocation& invocation) const = 0;
};

struct ToolRegistration {
  ToolDefinition definition;
  std::shared_ptr<IToolExecutor> executor;
  std::shared_ptr<IToolPresenter> presenter;
  QStringList legacy_aliases;
};

}  // namespace holonight_application
