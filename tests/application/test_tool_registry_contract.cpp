#include "holonight_application/tools/tool_presenters.h"
#include "holonight_application/tools/tool_registry.h"

#include <QJsonArray>
#include <QJsonObject>

#include <gtest/gtest.h>
#include <memory>

namespace holonight_application {
namespace {

class DummyHandle : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class JsonEchoExecutor : public IToolExecutor {
 public:
  explicit JsonEchoExecutor(QJsonObject result) : result_(std::move(result)) {}

  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& /*request*/,
                                             ToolOutcomeCallback on_finished) override {
    ToolOutcome outcome;
    outcome.result = result_;
    if (on_finished) {
      on_finished(outcome);
    }
    return std::make_shared<DummyHandle>();
  }

 private:
  QJsonObject result_;
};

TEST(ToolPresenters, GenericToolPresenterProvidesDeterministicRendererKeyAndDetailData) {
  GenericToolPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("ListFiles"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~")}},
      .status = holonight_domain::ToolInvocationStatus::Completed,
      .result = QJsonObject{{QStringLiteral("entries"),
                             QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("notes.txt")},
                                                    {QStringLiteral("type"), QStringLiteral("file")}}}}},
  };

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.renderer_key, QStringLiteral("filesystem.list"));
  EXPECT_EQ(presentation.title, QStringLiteral("ListFiles"));
  EXPECT_EQ(presentation.status_text, QStringLiteral("Completed"));
  EXPECT_FALSE(presentation.is_error);
  EXPECT_TRUE(presentation.detail_data.contains(QStringLiteral("arguments")));
  EXPECT_TRUE(presentation.detail_data.contains(QStringLiteral("result")));
}

TEST(ToolPresenters, ListFilesPresenterProducesCountsAndFileEntries) {
  ListFilesPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Documents")}},
      .status = holonight_domain::ToolInvocationStatus::Completed,
      .result =
          QJsonObject{
              {QStringLiteral("entries"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("notes.txt")},
                                                                 {QStringLiteral("type"), QStringLiteral("file")}},
                                                     QJsonObject{{QStringLiteral("name"), QStringLiteral("Projects")},
                                                                 {QStringLiteral("type"), QStringLiteral("directory")}},
                                                     QJsonObject{{QStringLiteral("name"), QStringLiteral("??")},
                                                                 {QStringLiteral("type"), QStringLiteral("unknown")}}}},
          },
  };

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.renderer_key, QStringLiteral("filesystem.list"));
  EXPECT_EQ(presentation.title, QStringLiteral("Listed ~/Documents"));
  EXPECT_EQ(presentation.summary, QStringLiteral("1 files, 1 folders"));
  EXPECT_EQ(presentation.detail_data.value(QStringLiteral("fileCount")).toInt(), 1);
  EXPECT_EQ(presentation.detail_data.value(QStringLiteral("directoryCount")).toInt(), 1);
}

TEST(ToolPresenters, ListFilesPresenterCountsEmptyAndMixedEntriesConsistently) {
  ListFilesPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Mixed")}},
      .status = holonight_domain::ToolInvocationStatus::Completed,
      .result =
          QJsonObject{
              {QStringLiteral("entries"),
               QJsonArray{
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("dir-a")},
                               {QStringLiteral("type"), QStringLiteral("directory")}},
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("file-a.txt")},
                               {QStringLiteral("type"), QStringLiteral("file")}},
                   QJsonObject{{QStringLiteral("name"), QStringLiteral("other")},
                               {QStringLiteral("type"), QStringLiteral("link")}},
               }},
          },
  };

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.summary, QStringLiteral("1 files, 1 folders"));
  const auto entries = presentation.detail_data.value(QStringLiteral("entries")).toList();
  EXPECT_EQ(entries.size(), 3);
  EXPECT_EQ(entries.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("unknown"));
  EXPECT_EQ(entries.at(2).toMap().value(QStringLiteral("iconName")).toString(), QStringLiteral("help"));
}

TEST(ToolPresenters, ListFilesPresenterMarksMalformedResultAsFailureWithFallbackMessage) {
  ListFilesPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Bad")}},
      .status = holonight_domain::ToolInvocationStatus::Failed,
      .error = holonight_domain::ToolError{.code = QStringLiteral("UNKNOWN_ERROR"),
                                           .message = QStringLiteral("Bad tool error")}};

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.title, QStringLiteral("Couldn’t list ~/Bad"));
  EXPECT_EQ(presentation.summary, QStringLiteral("Bad tool error"));
  EXPECT_TRUE(presentation.is_error);
}

TEST(ToolPresenters, ListFilesPresenterShowsNoEntriesForEmptyDirectoryResult) {
  ListFilesPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Empty")}},
      .status = holonight_domain::ToolInvocationStatus::Completed,
      .result = QJsonObject{{QStringLiteral("entries"), QJsonArray{}}},
  };

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.summary, QStringLiteral("No entries"));
  EXPECT_EQ(presentation.detail_data.value(QStringLiteral("fileCount")).toInt(), 0);
  EXPECT_EQ(presentation.detail_data.value(QStringLiteral("directoryCount")).toInt(), 0);
}

TEST(ToolPresenters, ListFilesPresenterFormatsErrorSummaries) {
  ListFilesPresenter presenter;
  const holonight_domain::ToolInvocation invocation{
      .tool_id = QStringLiteral("filesystem.list"),
      .function_name = QStringLiteral("list_files"),
      .arguments = QJsonObject{{QStringLiteral("path"), QStringLiteral("~/Nope")}},
      .status = holonight_domain::ToolInvocationStatus::Failed,
      .result = QJsonObject{{QStringLiteral("error"),
                             QJsonObject{{QStringLiteral("code"), QStringLiteral("NOT_FOUND")},
                                         {QStringLiteral("message"), QStringLiteral("No such file or directory.")}}}},
  };

  const ToolPresentation presentation = presenter.present(invocation);
  EXPECT_EQ(presentation.title, QStringLiteral("Couldn’t list ~/Nope"));
  EXPECT_EQ(presentation.summary, QStringLiteral("No such file or directory."));
  EXPECT_TRUE(presentation.is_error);
}

TEST(ToolRegistry, RegistrationSupportsCanonicalIdFunctionNameRendererAndAliasResolution) {
  ToolRegistration registration{
      .definition = ToolDefinition{.id = QStringLiteral("filesystem.list"),
                                   .function_name = QStringLiteral("list_files"),
                                   .display_name = QStringLiteral("List files"),
                                   .renderer_key = QStringLiteral("filesystem.list"),
                                   .description = QStringLiteral("List files in the home directory."),
                                   .input_schema = QJsonObject{},
                                   .risk = ToolDefinition::ToolRisk::Safe},
      .executor = std::make_shared<JsonEchoExecutor>(QJsonObject{{QStringLiteral("ok"), true}}),
      .presenter = std::make_shared<GenericToolPresenter>(),
      .legacy_aliases = {QStringLiteral("ListFiles")},
  };

  ToolRegistry registry;
  registry.registerTool(registration);
  const auto snapshot = registry.catalogSnapshot();
  ASSERT_EQ(snapshot.client_tools.size(), 1U);
  EXPECT_EQ(snapshot.client_tools.front().id, QStringLiteral("filesystem.list"));
  EXPECT_EQ(snapshot.client_tools.front().function_name, QStringLiteral("list_files"));
  EXPECT_EQ(snapshot.client_tools.front().renderer_key, QStringLiteral("filesystem.list"));

  const auto* regByFunction = registry.registrationByFunctionName(QStringLiteral("list_files"));
  ASSERT_NE(regByFunction, nullptr);
  EXPECT_EQ(regByFunction->definition.id, QStringLiteral("filesystem.list"));

  const QJsonObject legacyAliasResult = registry.invoke(QStringLiteral("ListFiles"), {});
  EXPECT_TRUE(legacyAliasResult.value(QStringLiteral("ok")).toBool());
  const QJsonObject functionResult = registry.invoke(QStringLiteral("list_files"), {});
  EXPECT_TRUE(functionResult.value(QStringLiteral("ok")).toBool());
  EXPECT_EQ(registry.tools().size(), 0U);
}

}  // namespace
}  // namespace holonight_application
