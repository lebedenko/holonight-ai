#include "holonight_application/tools/tool_registry.h"

#include "holonight_application/tools/tool_contracts.h"
#include "holonight_application/tools/tool_presenters.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <holonight_domain/tool_activity.h>
#include <memory>
#include <utility>

namespace holonight_application {
namespace {

class NoopToolExecutionHandle : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class LegacyExecutor : public IToolExecutor {
 public:
  explicit LegacyExecutor(std::shared_ptr<ITool> tool) : tool_(std::move(tool)) {}

  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    ToolOutcome outcome;
    if (!tool_) {
      outcome.error = holonight_domain::ToolError{.code = QStringLiteral("INTERNAL_ERROR"),
                                                  .message = QStringLiteral("Tool instance unavailable."),
                                                  .details = {}};
      if (on_finished) {
        on_finished(outcome);
      }
      return std::make_shared<NoopToolExecutionHandle>();
    }

    const QJsonObject parameters = request.parameters.toObject();
    QJsonObject toolResult;
    try {
      toolResult = tool_->execute(parameters);
    } catch (...) {
      outcome.error = holonight_domain::ToolError{.code = QStringLiteral("EXCEPTION"),
                                                  .message = QStringLiteral("Tool execution threw an exception."),
                                                  .details = {}};
      if (on_finished) {
        on_finished(outcome);
      }
      return std::make_shared<NoopToolExecutionHandle>();
    }
    outcome.result = toolResult;
    if (const QJsonValue error = toolResult.value(QStringLiteral("error")); error.isObject()) {
      const QJsonObject errorObject = error.toObject();
      outcome.error = holonight_domain::ToolError{
          .code = errorObject.value(QStringLiteral("code")).toString(),
          .message = errorObject.value(QStringLiteral("message")).toString(),
          .details = errorObject.value(QStringLiteral("details")),
      };
    }

    if (on_finished) {
      on_finished(outcome);
    }
    return std::make_shared<NoopToolExecutionHandle>();
  }

 private:
  std::shared_ptr<ITool> tool_;
};

[[nodiscard]] QJsonObject errorObject(const QString& code, const QString& message) {
  return QJsonObject{
      {QStringLiteral("code"), code}, {QStringLiteral("message"), message}, {QStringLiteral("details"), QJsonValue{}}};
}

[[nodiscard]] QJsonObject unknownToolError(const QString& name) {
  return errorObject(QStringLiteral("INVALID_TOOL"), QStringLiteral("Unknown tool: %1").arg(name));
}

[[nodiscard]] QJsonObject unknownFunctionError() {
  return errorObject(QStringLiteral("INVALID_INPUT"), QStringLiteral("Tool invocation request was malformed."));
}

void assertNoDuplicate(const QHash<QString, std::size_t>& map, const QString& key) {
  Q_ASSERT(!key.isEmpty());
  Q_ASSERT(!map.contains(key));
}

void assertNoDuplicate(const QStringList& aliases, const QHash<QString, std::size_t>& by_function_name,
                       const QHash<QString, std::size_t>& by_canonical_id,
                       const QHash<QString, std::size_t>& by_renderer_key,
                       const QHash<QString, std::size_t>& by_alias) {
  QSet<QString> unique_aliases;
  for (const QString& alias : aliases) {
    Q_ASSERT(!alias.isEmpty());
    Q_ASSERT(!unique_aliases.contains(alias));
    unique_aliases.insert(alias);
    Q_ASSERT(!by_function_name.contains(alias));
    Q_ASSERT(!by_canonical_id.contains(alias));
    Q_ASSERT(!by_renderer_key.contains(alias));
    Q_ASSERT(!by_alias.contains(alias));
  }
}

void validateRegistration(const ToolRegistration& registration, const QHash<QString, std::size_t>& by_function_name,
                          const QHash<QString, std::size_t>& by_canonical_id,
                          const QHash<QString, std::size_t>& by_renderer_key,
                          const QHash<QString, std::size_t>& by_alias) {
  Q_ASSERT(!registration.definition.id.isEmpty());
  Q_ASSERT(!registration.definition.function_name.isEmpty());
  Q_ASSERT(!registration.definition.renderer_key.isEmpty());
  Q_ASSERT(registration.executor != nullptr);
  Q_ASSERT(registration.presenter != nullptr);
  assertNoDuplicate(by_alias, registration.definition.id);
  assertNoDuplicate(by_function_name, registration.definition.id);
  assertNoDuplicate(by_canonical_id, registration.definition.id);
  assertNoDuplicate(by_renderer_key, registration.definition.id);

  assertNoDuplicate(by_alias, registration.definition.function_name);
  assertNoDuplicate(by_function_name, registration.definition.function_name);
  assertNoDuplicate(by_canonical_id, registration.definition.function_name);
  assertNoDuplicate(by_renderer_key, registration.definition.function_name);

  assertNoDuplicate(by_alias, registration.definition.renderer_key);
  assertNoDuplicate(by_renderer_key, registration.definition.renderer_key);

  for (const auto& alias : registration.legacy_aliases) {
    assertNoDuplicate(by_alias, alias);
    assertNoDuplicate(by_function_name, alias);
    assertNoDuplicate(by_canonical_id, alias);
    assertNoDuplicate(by_renderer_key, alias);
  }
}

}  // namespace

void ToolRegistry::registerTool(std::shared_ptr<ITool> tool) {
  Q_ASSERT(tool);
  Q_ASSERT(!tool->name().isEmpty());
  Q_ASSERT(!by_function_name_.contains(tool->name()));
  Q_ASSERT(!by_canonical_id_.contains(tool->name()));
  Q_ASSERT(!by_alias_.contains(tool->name()));
  ToolRegistration registration;
  registration.definition = ToolDefinition{.id = tool->name(),
                                           .function_name = tool->name(),
                                           .display_name = tool->name(),
                                           .renderer_key = QStringLiteral("generic"),
                                           .description = tool->description(),
                                           .input_schema = tool->schema(),
                                           .risk = ToolDefinition::ToolRisk::Safe};
  registration.executor = std::make_shared<LegacyExecutor>(tool);
  registration.presenter = std::make_shared<GenericToolPresenter>();
  registerToolInternal(std::move(registration), std::move(tool));
}

void ToolRegistry::registerTool(ToolRegistration registration) {
  validateRegistration(registration, by_function_name_, by_canonical_id_, by_renderer_key_, by_alias_);
  registerToolInternal(std::move(registration), nullptr);
}

const ToolRegistration* ToolRegistry::registrationByFunctionName(const QString& name) const {
  const RegisteredTool* record = findRecord(name);
  return record == nullptr ? nullptr : &record->registration;
}

void ToolRegistry::registerToolInternal(ToolRegistration registration, std::shared_ptr<ITool> legacy_tool) {
  const std::size_t index = registrations_.size();
  registrations_.push_back(
      RegisteredTool{.registration = std::move(registration), .legacy_tool = std::move(legacy_tool)});

  by_function_name_.insert(registrations_[index].registration.definition.function_name, index);
  by_canonical_id_.insert(registrations_[index].registration.definition.id, index);
  by_renderer_key_.insert(registrations_[index].registration.definition.renderer_key, index);
  for (const QString& alias : registrations_[index].registration.legacy_aliases) {
    if (!alias.isEmpty()) {
      by_alias_.insert(alias, index);
    }
  }

  if (registrations_[index].legacy_tool != nullptr) {
    tools_.push_back(registrations_[index].legacy_tool);
  }
}

const ToolRegistry::RegisteredTool* ToolRegistry::findRecord(const QString& name) const {
  if (const auto iterator = by_function_name_.find(name); iterator != by_function_name_.end()) {
    const auto index = iterator.value();
    if (index < registrations_.size()) {
      return &registrations_[index];
    }
  }
  if (const auto iterator = by_canonical_id_.find(name); iterator != by_canonical_id_.end()) {
    const auto index = iterator.value();
    if (index < registrations_.size()) {
      return &registrations_[index];
    }
  }
  if (const auto iterator = by_alias_.find(name); iterator != by_alias_.end()) {
    const auto index = iterator.value();
    if (index < registrations_.size()) {
      return &registrations_[index];
    }
  }
  return nullptr;
}

const std::vector<std::shared_ptr<ITool>>& ToolRegistry::tools() const { return tools_; }

std::shared_ptr<ITool> ToolRegistry::find(const QString& name) const {
  const RegisteredTool* record = findRecord(name);
  return record == nullptr ? nullptr : record->legacy_tool;
}

QJsonObject ToolRegistry::invoke(const QString& name, const QJsonObject& parameters) const {
  const RegisteredTool* record = findRecord(name);
  if (record == nullptr) {
    return QJsonObject{{QStringLiteral("error"), unknownToolError(name)}};
  }

  if (!record->registration.executor) {
    return QJsonObject{{QStringLiteral("error"), unknownFunctionError()}};
  }

  ToolOutcome outcome;
  bool completed = false;
  static_cast<void>(record->registration.executor->start(
      ToolExecutionRequest{.invocation_id = name,
                           .provider_call_id = name,
                           .function_name = record->registration.definition.function_name,
                           .parameters = parameters},
      [&outcome, &completed](const ToolOutcome& toolOutcome) {
        outcome = toolOutcome;
        completed = true;
      }));

  if (!completed) {
    return errorObject(QStringLiteral("EXECUTION_FAILED"), QStringLiteral("Tool execution did not complete."));
  }

  if (outcome.result.isObject()) {
    const auto resultObject = outcome.result.toObject();
    if (!outcome.error.has_value()) {
      return resultObject;
    }
    return resultObject;
  }

  if (outcome.error.has_value()) {
    const auto& toolError = *outcome.error;
    QJsonObject error = errorObject(toolError.code, toolError.message);
    if (!toolError.details.isNull() && !toolError.details.isUndefined()) {
      error.insert(QStringLiteral("details"), toolError.details);
    }
    return error;
  }

  if (outcome.result.isArray()) {
    QJsonObject object;
    object.insert(QStringLiteral("result"), QJsonValue(outcome.result.toArray()));
    return object;
  }
  if (!outcome.result.isUndefined() && !outcome.result.isNull()) {
    QJsonObject object;
    object.insert(QStringLiteral("result"), QJsonValue::fromVariant(outcome.result.toVariant()));
    return object;
  }
  return {};
}

holonight_domain::ToolCatalogSnapshot ToolRegistry::catalogSnapshot() const {
  holonight_domain::ToolCatalogSnapshot snapshot;
  snapshot.client_tools.reserve(registrations_.size());
  for (const auto& registered_tool : registrations_) {
    const auto& definition = registered_tool.registration.definition;
    snapshot.client_tools.push_back(holonight_domain::ToolDefinition{
        .id = definition.id,
        .function_name = definition.function_name,
        .display_name = definition.display_name,
        .renderer_key = definition.renderer_key,
        .description = definition.description,
        .input_schema = definition.input_schema,
        .legacy_aliases = registered_tool.registration.legacy_aliases,
        .risk = static_cast<holonight_domain::ToolDefinition::ToolRisk>(definition.risk),
    });
  }
  return snapshot;
}

}  // namespace holonight_application
