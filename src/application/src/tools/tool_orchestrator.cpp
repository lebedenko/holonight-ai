#include "holonight_application/tools/tool_orchestrator.h"

#include "holonight_application/tools/tool_registry.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>

namespace {

using holonight_application::ToolOutcome;
using holonight_domain::ToolError;
using holonight_domain::ToolInvocation;
using holonight_domain::ToolInvocationStatus;

[[nodiscard]] bool hasResultError(const QJsonObject& result, ToolError& error) {
  const QJsonValue errorValue = result.value(QStringLiteral("error"));
  if (!errorValue.isObject()) {
    return false;
  }

  const QJsonObject errorObject = errorValue.toObject();
  error = ToolError{
      .code = errorObject.value(QStringLiteral("code")).toString(),
      .message = errorObject.value(QStringLiteral("message")).toString(),
      .details = errorObject.value(QStringLiteral("details")),
  };
  return true;
}

void deliverOnApplicationThread(std::function<void()> callback) {
  if (!callback) {
    return;
  }

  QObject* app = QCoreApplication::instance();
  if (app == nullptr || QThread::currentThread() == app->thread()) {
    callback();
    return;
  }

  QMetaObject::invokeMethod(app, std::move(callback), Qt::QueuedConnection);
}

[[nodiscard]] bool parseOutcomeError(const ToolOutcome& outcome, ToolError& error) {
  if (outcome.error.has_value()) {
    error = *outcome.error;
    return true;
  }

  if (!outcome.result.isObject()) {
    return false;
  }

  ToolError parsedError;
  if (!hasResultError(outcome.result.toObject(), parsedError)) {
    return false;
  }

  error = parsedError;
  return true;
}

ToolInvocation baseInvocation(const holonight_application::ToolRegistry& registry,
                              const holonight_application::ToolOrchestrator::ExecutionRequest& request) {
  ToolInvocation invocation;
  invocation.id = request.invocation_id;
  invocation.provider_call_id = request.provider_call_id;
  invocation.arguments = request.arguments;
  invocation.requested_at = request.requested_at;
  invocation.location = holonight_domain::ToolExecutionLocation::LocalClient;

  if (const auto* registration = registry.registrationByFunctionName(request.function_name)) {
    invocation.tool_id = registration->definition.id;
    invocation.function_name = registration->definition.function_name;
  } else {
    invocation.function_name = request.function_name;
  }

  return invocation;
}

ToolInvocation terminalFromOutcome(ToolInvocation invocation, const ToolOutcome& outcome) {
  invocation.finished_at = QDateTime::currentDateTimeUtc();

  ToolError error;
  if (parseOutcomeError(outcome, error)) {
    static_cast<void>(invocation.transitionTo(ToolInvocationStatus::Failed));
    invocation.error = error;
    if (!outcome.result.isUndefined() && !outcome.result.isNull()) {
      invocation.result = outcome.result;
    } else if (!invocation.result.has_value()) {
      invocation.result = QJsonObject{};
    }
    return invocation;
  }

  if (outcome.result.isObject() || outcome.result.isArray() ||
      (!outcome.result.isUndefined() && !outcome.result.isNull())) {
    invocation.result = outcome.result;
  }

  static_cast<void>(invocation.transitionTo(ToolInvocationStatus::Completed));
  return invocation;
}

ToolInvocation cancelledFromInvocation(const ToolInvocation& invocation) {
  ToolInvocation cancelled = invocation;
  cancelled.finished_at = QDateTime::currentDateTimeUtc();
  (void)cancelled.transitionTo(ToolInvocationStatus::Cancelled);
  return cancelled;
}

class NoopHandle final : public holonight_application::ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class OrchestratorHandle final : public holonight_application::ToolExecutionHandle {
 public:
  OrchestratorHandle(std::shared_ptr<ToolInvocation> invocation, std::shared_ptr<std::atomic_bool> terminal_emitted,
                     std::shared_ptr<std::atomic_bool> cancel_requested,
                     holonight_application::ToolExecutionHandlePtr delegate,
                     std::function<void(const ToolInvocation&)> on_terminal)
      : invocation_(std::move(invocation)),
        terminal_emitted_(std::move(terminal_emitted)),
        cancel_requested_(std::move(cancel_requested)),
        delegate_(std::move(delegate)),
        on_terminal_(std::move(on_terminal)) {}

  [[nodiscard]] bool canCancel() const override {
    return delegate_ != nullptr && delegate_->canCancel() && !terminal_emitted_->load(std::memory_order_acquire);
  }

  void cancel() override {
    bool expected = false;
    if (!cancel_requested_->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return;
    }

    if (terminal_emitted_->exchange(true, std::memory_order_acq_rel)) {
      return;
    }

    const ToolInvocation cancelled = cancelledFromInvocation(*invocation_);
    deliverOnApplicationThread([callback = on_terminal_, invocation = cancelled]() {
      if (callback) {
        callback(invocation);
      }
    });

    if (delegate_ != nullptr) {
      delegate_->cancel();
    }
  }

 private:
  std::shared_ptr<ToolInvocation> invocation_;
  std::shared_ptr<std::atomic_bool> terminal_emitted_;
  std::shared_ptr<std::atomic_bool> cancel_requested_;
  holonight_application::ToolExecutionHandlePtr delegate_;
  std::function<void(const ToolInvocation&)> on_terminal_;
};

}  // namespace

namespace holonight_application {

ToolExecutionHandlePtr ToolOrchestrator::execute(const ToolRegistry& registry, const ExecutionRequest& request,
                                                 const Callbacks& callbacks) {
  auto invocation = std::make_shared<ToolInvocation>(baseInvocation(registry, request));

  const auto terminal_emitted = std::make_shared<std::atomic_bool>(false);
  const auto cancel_requested = std::make_shared<std::atomic_bool>(false);

  auto emitRunning = [invocation, callbacks](const ToolInvocation& runningInvocation) {
    if (callbacks.on_running == nullptr) {
      return;
    }

    deliverOnApplicationThread(
        [callback = callbacks.on_running, invocation = runningInvocation]() { callback(invocation); });
  };

  auto emitTerminal = [callbacks, invocation_ptr = invocation, terminal_emitted](const ToolOutcome& outcome) mutable {
    ToolInvocation terminal = terminalFromOutcome(*invocation_ptr, outcome);
    if (terminal_emitted->exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    if (callbacks.on_terminal == nullptr) {
      return;
    }

    deliverOnApplicationThread([callback = callbacks.on_terminal, invocation = terminal]() { callback(invocation); });
  };

  const auto publishError = [invocation, callbacks, terminal_emitted](const QString& code, const QString& message) {
    if (terminal_emitted->exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    ToolInvocation terminal = *invocation;
    terminal.finished_at = QDateTime::currentDateTimeUtc();
    static_cast<void>(terminal.transitionTo(ToolInvocationStatus::Failed));
    terminal.error = ToolError{.code = code, .message = message, .details = {}};
    if (callbacks.on_terminal != nullptr) {
      deliverOnApplicationThread([callback = callbacks.on_terminal, terminalInvocation = std::move(terminal)]() {
        callback(terminalInvocation);
      });
    }
  };

  const auto* const registryRecord = registry.registrationByFunctionName(request.function_name);
  if (registryRecord == nullptr || registryRecord->executor == nullptr) {
    const QString name = request.function_name;
    publishError(QStringLiteral("INVALID_TOOL"), QStringLiteral("Unknown tool: %1").arg(name));
    return std::make_shared<NoopHandle>();
  }

  if (invocation->transitionTo(ToolInvocationStatus::Running).has_value() && !invocation->started_at.has_value()) {
    invocation->started_at = QDateTime::currentDateTimeUtc();
  }
  emitRunning(*invocation);

  ToolExecutionHandlePtr execution_handle;
  invocation->finished_at.reset();

  try {
    execution_handle = registryRecord->executor->start(
        ToolExecutionRequest{
            .invocation_id = request.invocation_id,
            .provider_call_id = request.provider_call_id,
            .function_name = request.function_name,
            .parameters = request.arguments,
        },
        emitTerminal);
  } catch (const std::exception& ex) {
    Q_UNUSED(ex);
    publishError(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("Tool execution threw an exception."));
    return std::make_shared<NoopHandle>();
  } catch (...) {
    publishError(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("Tool execution threw an exception."));
    return std::make_shared<NoopHandle>();
  }

  if (!execution_handle) {
    execution_handle = std::make_shared<NoopHandle>();
  }

  invocation->can_cancel = execution_handle->canCancel();
  if (callbacks.on_capabilities_changed != nullptr) {
    deliverOnApplicationThread([callback = callbacks.on_capabilities_changed, invocation]() { callback(*invocation); });
  }

  return std::make_unique<OrchestratorHandle>(invocation, terminal_emitted, cancel_requested, execution_handle,
                                              callbacks.on_terminal);
}

}  // namespace holonight_application
