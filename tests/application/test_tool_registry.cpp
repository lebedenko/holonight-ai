#include "holonight_application/tools/tool_registry.h"

#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

class FakeTool : public ITool {
 public:
  explicit FakeTool(QString name) : name_(std::move(name)) {}

  [[nodiscard]] QString name() const override { return name_; }
  [[nodiscard]] QString description() const override { return QStringLiteral("A fake tool for tests."); }
  [[nodiscard]] QJsonObject schema() const override {
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
  }
  [[nodiscard]] QJsonObject execute(const QJsonObject& parameters) override {
    return QJsonObject{{QStringLiteral("echo"), parameters}};
  }

 private:
  QString name_;
};

TEST(ToolRegistry, FindReturnsNullptrForUnknownTool) {
  EXPECT_EQ(ToolRegistry().find(QStringLiteral("Missing")), nullptr);
}

TEST(ToolRegistry, RegisterToolMakesItFindableAndInvocable) {
  ToolRegistry registry;
  registry.registerTool(std::make_shared<FakeTool>(QStringLiteral("Fake")));

  ASSERT_NE(registry.find(QStringLiteral("Fake")), nullptr);
  const QJsonObject result = registry.invoke(QStringLiteral("Fake"), QJsonObject{{QStringLiteral("x"), 1}});
  EXPECT_EQ(result.value(QStringLiteral("echo")).toObject().value(QStringLiteral("x")).toInt(), 1);
}

TEST(ToolRegistry, InvokeUnknownToolReturnsStructuredErrorInsteadOfCrashing) {
  ToolRegistry registry;
  const QJsonObject result = registry.invoke(QStringLiteral("DoesNotExist"), QJsonObject{});
  EXPECT_TRUE(result.contains(QStringLiteral("error")));
  EXPECT_EQ(result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
            QStringLiteral("INVALID_TOOL"));
}

TEST(ToolRegistry, ToolsPreservesRegistrationOrder) {
  ToolRegistry registry;
  registry.registerTool(std::make_shared<FakeTool>(QStringLiteral("First")));
  registry.registerTool(std::make_shared<FakeTool>(QStringLiteral("Second")));

  ASSERT_EQ(registry.tools().size(), 2U);
  EXPECT_EQ(registry.tools()[0]->name(), QStringLiteral("First"));
  EXPECT_EQ(registry.tools()[1]->name(), QStringLiteral("Second"));
}

TEST(ToolRegistry, CatalogSnapshotIsProviderNeutral) {
  ToolRegistry registry;
  registry.registerTool(std::make_shared<FakeTool>(QStringLiteral("Fake")));

  const auto catalog = registry.catalogSnapshot();
  ASSERT_EQ(catalog.client_tools.size(), 1U);
  EXPECT_EQ(catalog.client_tools.front().function_name, QStringLiteral("Fake"));
  EXPECT_EQ(catalog.client_tools.front().description, QStringLiteral("A fake tool for tests."));
  EXPECT_EQ(catalog.client_tools.front().input_schema.value(QStringLiteral("type")).toString(),
            QStringLiteral("object"));
}

}  // namespace
}  // namespace holonight_application
