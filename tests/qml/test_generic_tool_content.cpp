#include <QDir>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

std::unique_ptr<QObject> createContent(QQmlEngine& engine, const QVariantMap& tool_call) {
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/GenericToolContent.qml"));
  QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
  if (!component.isReady()) {
    ADD_FAILURE() << qPrintable(component.errorString());
    return {};
  }
  return std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("toolCall"), tool_call}}));
}

void configureEngine(QQmlEngine& engine) {
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
}

TEST(GenericToolContentQml, UnknownToolArgumentsRemainReadable) {
  QQmlEngine engine;
  configureEngine(engine);
  auto content = createContent(
      engine, {{QStringLiteral("kind"), QStringLiteral("invocation")},
               {QStringLiteral("input"), QVariantMap{{QStringLiteral("legacy"), true}, {QStringLiteral("count"), 2}}}});
  ASSERT_NE(content, nullptr);

  auto* payload = content->findChild<QObject*>(QStringLiteral("genericToolPayload"));
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->property("text").toString(), QStringLiteral("{\n  \"count\": 2,\n  \"legacy\": true\n}"));
  EXPECT_TRUE(payload->property("selectByMouse").toBool());
}

TEST(GenericToolContentQml, MissingHistoricalPayloadUsesSafeExplanation) {
  QQmlEngine engine;
  configureEngine(engine);
  auto content = createContent(engine, {{QStringLiteral("kind"), QStringLiteral("result")}});
  ASSERT_NE(content, nullptr);

  auto* payload = content->findChild<QObject*>(QStringLiteral("genericToolPayload"));
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->property("text").toString(), QStringLiteral("No raw data available."));
}

}  // namespace
