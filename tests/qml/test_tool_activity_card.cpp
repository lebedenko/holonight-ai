#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

std::unique_ptr<QObject> createCard(QQmlEngine& engine, QVariantMap tool_call) {
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ToolActivityCard.qml"));
  QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
  if (!component.isReady()) {
    ADD_FAILURE() << qPrintable(component.errorString());
    return {};
  }
  return std::unique_ptr<QObject>(component.createWithInitialProperties(
      {{QStringLiteral("toolCall"), std::move(tool_call)},
       {QStringLiteral("createdAt"), QDateTime::fromString(QStringLiteral("2026-08-05T12:00:00Z"), Qt::ISODate)}}));
}

void configureEngine(QQmlEngine& engine) {
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
}

QVariantMap completedToolCall() {
  return {{QStringLiteral("toolUseId"), QStringLiteral("tool-1")},
          {QStringLiteral("kind"), QStringLiteral("invocation")},
          {QStringLiteral("status"), QStringLiteral("completed")},
          {QStringLiteral("functionName"), QStringLiteral("list_files")},
          {QStringLiteral("rendererKey"), QStringLiteral("filesystem.list")},
          {QStringLiteral("toolTitle"), QStringLiteral("Listed /tmp")},
          {QStringLiteral("summary"), QStringLiteral("1 file")},
          {QStringLiteral("detailData"), QVariantMap{{QStringLiteral("path"), QStringLiteral("/tmp")},
                                                     {QStringLiteral("fileCount"), 1},
                                                     {QStringLiteral("directoryCount"), 0},
                                                     {QStringLiteral("entries"), QVariantList{}}}},
          {QStringLiteral("rawArgumentsJson"), QStringLiteral(R"({"path":"/tmp"})")},
          {QStringLiteral("rawResultJson"), QStringLiteral(R"({"entries":[{"name":"complete.txt"}]})")}};
}

TEST(ToolActivityCardQml, RawDetailsAreHiddenUntilExplicitlyOpenedAndRemainSelectable) {
  QQmlEngine engine;
  configureEngine(engine);
  auto card = createCard(engine, completedToolCall());
  ASSERT_NE(card, nullptr);
  card->setProperty("width", 800);
  card->setProperty("expanded", true);
  QCoreApplication::processEvents();

  auto* details = card->findChild<QObject*>(QStringLiteral("toolActivityRawDetails"));
  auto* disclosure = card->findChild<QObject*>(QStringLiteral("toolActivityRawDisclosure"));
  auto* arguments = card->findChild<QObject*>(QStringLiteral("toolActivityRawArguments"));
  auto* result = card->findChild<QObject*>(QStringLiteral("toolActivityRawResult"));
  ASSERT_NE(details, nullptr);
  ASSERT_NE(disclosure, nullptr);
  ASSERT_NE(arguments, nullptr);
  ASSERT_NE(result, nullptr);
  EXPECT_FALSE(details->property("visible").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(disclosure, "clicked"));
  EXPECT_TRUE(details->property("visible").toBool());
  EXPECT_EQ(arguments->property("text").toString(), QStringLiteral(R"({"path":"/tmp"})"));
  EXPECT_EQ(result->property("text").toString(), QStringLiteral(R"({"entries":[{"name":"complete.txt"}]})"));
  EXPECT_TRUE(arguments->property("selectByMouse").toBool());
  EXPECT_TRUE(result->property("selectByMouse").toBool());
}

TEST(ToolActivityCardQml, CopyResultUsesCompleteUntruncatedPayloadAndHasAccessibleName) {
  QQmlEngine engine;
  configureEngine(engine);
  const QString complete_result = QStringLiteral(R"({"entries":[{"name":"first"},{"name":"last"}]})");
  QVariantMap tool_call = completedToolCall();
  tool_call.insert(QStringLiteral("rawResultJson"), complete_result);
  auto card = createCard(engine, tool_call);
  ASSERT_NE(card, nullptr);
  card->setProperty("expanded", true);

  auto* copy = card->findChild<QObject*>(QStringLiteral("toolActivityCopyResult"));
  ASSERT_NE(copy, nullptr);
  auto* accessible = QAccessible::queryAccessibleInterface(copy);
  ASSERT_NE(accessible, nullptr);
  EXPECT_EQ(accessible->text(QAccessible::Name), QStringLiteral("Copy complete tool result"));
  QGuiApplication::clipboard()->clear();
  ASSERT_TRUE(QMetaObject::invokeMethod(copy, "clicked"));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), complete_result);
}

TEST(ToolActivityCardQml, MalformedHistoricalValuesHaveSafeRawFallback) {
  QQmlEngine engine;
  configureEngine(engine);
  QVariantMap tool_call = completedToolCall();
  tool_call.remove(QStringLiteral("rawArgumentsJson"));
  tool_call.remove(QStringLiteral("rawResultJson"));
  tool_call.insert(QStringLiteral("input"), QVariantMap{{QStringLiteral("path"), QStringLiteral("/legacy")}});
  tool_call.insert(QStringLiteral("result"), QStringLiteral("not-json-but-readable"));
  auto card = createCard(engine, tool_call);
  ASSERT_NE(card, nullptr);

  EXPECT_EQ(card->property("rawArgumentsText").toString(), QStringLiteral("{\n  \"path\": \"/legacy\"\n}"));
  EXPECT_EQ(card->property("rawResultText").toString(), QStringLiteral("not-json-but-readable"));
}

TEST(ToolActivityCardQml, HidingDetailsImmediatelyRestoresCompactSingleRowHeight) {
  QQmlEngine engine;
  configureEngine(engine);
  auto card = createCard(engine, completedToolCall());
  ASSERT_NE(card, nullptr);
  card->setProperty("width", 800);
  QCoreApplication::processEvents();

  auto* disclosure = card->findChild<QObject*>(QStringLiteral("toolActivityDisclosure"));
  auto* status = card->findChild<QObject*>(QStringLiteral("toolActivityStatus"));
  auto* function = card->findChild<QObject*>(QStringLiteral("toolActivityFunction"));
  auto* loader = card->findChild<QObject*>(QStringLiteral("toolActivityRendererLoader"));
  ASSERT_NE(disclosure, nullptr);
  ASSERT_NE(status, nullptr);
  ASSERT_NE(function, nullptr);
  ASSERT_NE(loader, nullptr);
  const qreal collapsedHeight = card->property("implicitHeight").toReal();
  EXPECT_FALSE(status->property("visible").toBool());
  EXPECT_FALSE(function->property("visible").toBool());
  EXPECT_FALSE(loader->property("active").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(disclosure, "clicked"));
  QCoreApplication::processEvents();
  EXPECT_TRUE(status->property("visible").toBool());
  EXPECT_TRUE(loader->property("active").toBool());

  ASSERT_TRUE(QMetaObject::invokeMethod(disclosure, "clicked"));
  QCoreApplication::processEvents();
  EXPECT_FALSE(status->property("visible").toBool());
  EXPECT_FALSE(loader->property("active").toBool());
  EXPECT_DOUBLE_EQ(card->property("implicitHeight").toReal(), collapsedHeight);
}

}  // namespace
