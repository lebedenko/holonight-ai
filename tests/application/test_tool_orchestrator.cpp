#include "holonight_application/tools/i_tool.h"
#include "holonight_application/tools/list_files_tool.h"
#include "holonight_application/tools/tool_orchestrator.h"
#include "holonight_application/tools/tool_presenters.h"
#include "holonight_application/tools/tool_registry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QTest>
#include <QThread>

#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace holonight_application {
namespace {

class NoopHandle : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class RecordingHandle : public ToolExecutionHandle {
 public:
  explicit RecordingHandle(std::shared_ptr<std::atomic_bool> canceled) : canceled_(std::move(canceled)) {}

  [[nodiscard]] bool canCancel() const override { return true; }
  void cancel() override { canceled_->store(true); }

 private:
  std::shared_ptr<std::atomic_bool> canceled_;
};

class MultiFinishExecutor : public IToolExecutor {
 public:
  explicit MultiFinishExecutor(bool duplicate_terminal) : duplicate_terminal_(duplicate_terminal) {}

  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    (void)request;
    auto on_finished_ptr = std::make_shared<ToolOutcomeCallback>(std::move(on_finished));
    const bool duplicate = duplicate_terminal_;
    std::thread([on_finished_ptr, duplicate]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      if (on_finished_ptr && *on_finished_ptr) {
        ToolOutcome first;
        first.result = QJsonObject{{QStringLiteral("value"), QStringLiteral("first")}};
        (*on_finished_ptr)(first);
      }
      if (duplicate && on_finished_ptr && *on_finished_ptr) {
        ToolOutcome second;
        second.result = QJsonObject{{QStringLiteral("value"), QStringLiteral("second")}};
        (*on_finished_ptr)(second);
      }
    }).detach();
    return std::make_shared<NoopHandle>();
  }

 private:
  bool duplicate_terminal_;
};

class ThrowingExecutor : public IToolExecutor {
 public:
  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    (void)request;
    (void)on_finished;
    throw std::runtime_error("tool execution failed");
  }
};

class SlowCancellableExecutor : public IToolExecutor {
 public:
  explicit SlowCancellableExecutor(std::shared_ptr<std::atomic_bool> canceled) : canceled_(std::move(canceled)) {}

  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    (void)request;
    auto on_finished_ptr = std::make_shared<ToolOutcomeCallback>(std::move(on_finished));
    std::thread([on_finished_ptr]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      if (on_finished_ptr && *on_finished_ptr) {
        ToolOutcome lateOutcome;
        lateOutcome.result = QJsonObject{{QStringLiteral("value"), QStringLiteral("done")}};
        (*on_finished_ptr)(lateOutcome);
      }
    }).detach();
    return std::make_shared<RecordingHandle>(canceled_);
  }

 private:
  std::shared_ptr<std::atomic_bool> canceled_;
};

ToolRegistration testRegistration(const QString& registration_id, const QString& function_name,
                                  std::shared_ptr<IToolExecutor> executor) {
  return ToolRegistration{
      .definition = {.id = registration_id,
                     .function_name = function_name,
                     .display_name = function_name,
                     .renderer_key = QStringLiteral("generic"),
                     .description = QStringLiteral("test tool"),
                     .input_schema = QJsonObject{},
                     .risk = holonight_application::ToolDefinition::ToolRisk::Safe},
      .executor = std::move(executor),
      .presenter = std::make_shared<GenericToolPresenter>(),
  };
}

TEST(ToolOrchestrator, ListFilesExecutorRunsOffMainThread) {
  ToolRegistry registry;
  registry.registerTool(listFilesToolRegistration());

  const ToolRegistration* registration = registry.registrationByFunctionName(QStringLiteral("list_files"));
  ASSERT_NE(registration, nullptr);
  ASSERT_NE(registration->executor, nullptr);

  auto callback_thread_promise = std::make_shared<std::promise<std::thread::id>>();
  auto callback_thread_future = callback_thread_promise->get_future();
  const ToolExecutionRequest request{.invocation_id = QStringLiteral("invocation-listfiles-01"),
                                     .provider_call_id = QStringLiteral("toolu_01"),
                                     .function_name = QStringLiteral("list_files"),
                                     .parameters = QJsonObject{{QStringLiteral("path"), QStringLiteral(".")}}};

  const ToolExecutionHandlePtr handle =
      registration->executor->start(request, [promise = callback_thread_promise](const ToolOutcome&) {
        try {
          promise->set_value(std::this_thread::get_id());
        } catch (...) {
        }
      });

  EXPECT_NE(handle, nullptr);
  EXPECT_FALSE(handle->canCancel());

  ASSERT_EQ(callback_thread_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_NE(callback_thread_future.get(), std::this_thread::get_id());
}

TEST(ToolOrchestrator, RunningAndTerminalCallbacksUseOrderedLifecycleTransitions) {
  ToolRegistry registry;
  registry.registerTool(testRegistration(QStringLiteral("tool.running"), QStringLiteral("running_tool"),
                                         std::make_shared<MultiFinishExecutor>(true)));

  ToolOrchestrator orchestrator;
  const ToolOrchestrator::ExecutionRequest request{.invocation_id = QStringLiteral("invocation-running-1"),
                                                   .provider_call_id = QStringLiteral("toolu_running"),
                                                   .function_name = QStringLiteral("running_tool"),
                                                   .arguments = QJsonObject{},
                                                   .requested_at = QDateTime::currentDateTimeUtc()};

  std::vector<holonight_domain::ToolInvocationStatus> status_log;
  std::vector<holonight_domain::ToolInvocationStatus> terminal_statuses;
  std::vector<QThread*> callback_threads;
  const ToolOrchestrator::Callbacks callbacks{
      .on_running =
          [&status_log, &callback_threads](const holonight_domain::ToolInvocation& invocation) {
            status_log.push_back(invocation.status);
            callback_threads.push_back(QThread::currentThread());
          },
      .on_terminal =
          [&terminal_statuses, &callback_threads](const holonight_domain::ToolInvocation& invocation) {
            terminal_statuses.push_back(invocation.status);
            callback_threads.push_back(QThread::currentThread());
          },
  };

  const ToolExecutionHandlePtr handle = orchestrator.execute(registry, request, callbacks);
  ASSERT_NE(handle, nullptr);

  for (int i = 0; i < 100 && terminal_statuses.empty(); ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }

  ASSERT_FALSE(status_log.empty());
  ASSERT_FALSE(terminal_statuses.empty());
  EXPECT_EQ(status_log.front(), holonight_domain::ToolInvocationStatus::Running);
  EXPECT_EQ(terminal_statuses.size(), 1U);
  EXPECT_EQ(terminal_statuses.front(), holonight_domain::ToolInvocationStatus::Completed);
  ASSERT_FALSE(callback_threads.empty());
  EXPECT_EQ(callback_threads.back(), QThread::currentThread());
}

TEST(ToolOrchestrator, CancelledHandleSuppressesLateExecutorCallbacks) {
  const auto canceled = std::make_shared<std::atomic_bool>(false);
  ToolRegistry registry;
  registry.registerTool(testRegistration(QStringLiteral("tool.cancel"), QStringLiteral("cancel_tool"),
                                         std::make_shared<SlowCancellableExecutor>(canceled)));

  ToolOrchestrator orchestrator;
  const ToolOrchestrator::ExecutionRequest request{.invocation_id = QStringLiteral("invocation-cancel-1"),
                                                   .provider_call_id = QStringLiteral("toolu_cancel"),
                                                   .function_name = QStringLiteral("cancel_tool"),
                                                   .arguments = QJsonObject{},
                                                   .requested_at = QDateTime::currentDateTimeUtc()};

  std::vector<holonight_domain::ToolInvocationStatus> terminal_statuses;
  bool cancellation_capability_published = false;
  const ToolOrchestrator::Callbacks callbacks{
      .on_capabilities_changed =
          [&cancellation_capability_published](const holonight_domain::ToolInvocation& invocation) {
            cancellation_capability_published = invocation.can_cancel;
          },
      .on_terminal =
          [&terminal_statuses](const holonight_domain::ToolInvocation& invocation) {
            terminal_statuses.push_back(invocation.status);
          },
  };

  const ToolExecutionHandlePtr handle = orchestrator.execute(registry, request, callbacks);
  ASSERT_NE(handle, nullptr);
  ASSERT_TRUE(handle->canCancel());
  EXPECT_TRUE(cancellation_capability_published);

  handle->cancel();
  EXPECT_TRUE(canceled->load());

  for (int i = 0; i < 120 && terminal_statuses.empty(); ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(5);
  }

  EXPECT_EQ(terminal_statuses,
            std::vector<holonight_domain::ToolInvocationStatus>{holonight_domain::ToolInvocationStatus::Cancelled});
}

TEST(ToolOrchestrator, ExecutorThrowPublishesFailedTerminalState) {
  ToolRegistry registry;
  registry.registerTool(testRegistration(QStringLiteral("tool.throw"), QStringLiteral("throw_tool"),
                                         std::make_shared<ThrowingExecutor>()));

  ToolOrchestrator orchestrator;
  const ToolOrchestrator::ExecutionRequest request{.invocation_id = QStringLiteral("invocation-throw-1"),
                                                   .provider_call_id = QStringLiteral("toolu_throw"),
                                                   .function_name = QStringLiteral("throw_tool"),
                                                   .arguments = QJsonObject{},
                                                   .requested_at = QDateTime::currentDateTimeUtc()};

  std::vector<holonight_domain::ToolInvocationStatus> terminal_statuses;
  std::vector<QString> terminal_errors;
  const ToolOrchestrator::Callbacks callbacks{
      .on_terminal =
          [&terminal_statuses, &terminal_errors](const holonight_domain::ToolInvocation& invocation) {
            terminal_statuses.push_back(invocation.status);
            ASSERT_TRUE(invocation.error.has_value());
            terminal_errors.push_back(invocation.error->code);
          },
  };

  ASSERT_NE(orchestrator.execute(registry, request, callbacks), nullptr);

  for (int i = 0; i < 50 && terminal_statuses.empty(); ++i) {
    QCoreApplication::processEvents();
    QTest::qWait(10);
  }

  EXPECT_EQ(terminal_statuses,
            std::vector<holonight_domain::ToolInvocationStatus>{holonight_domain::ToolInvocationStatus::Failed});
  ASSERT_EQ(terminal_errors.size(), 1U);
  EXPECT_EQ(terminal_errors.front(), QStringLiteral("INTERNAL_ERROR"));
}

}  // namespace
}  // namespace holonight_application
