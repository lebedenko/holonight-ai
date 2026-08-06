#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QtQml>

#include <gtest/gtest.h>
#include <holonight_application/token_formatter.h>
#include <memory>

namespace {

class ResponseStatsFooterQml : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterTypesAndRevisions<holonight_application::TokenFormatter>("HolonightChat", 1);
  }

  void SetUp() override {
    engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  }

  std::unique_ptr<QObject> create() {
    const QString path =
        QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ResponseStatsFooter.qml"));
    QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
    EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
    if (!component.isReady()) {
      return {};
    }

    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{QStringLiteral("isAssistant"), true},
                                               {QStringLiteral("messageStatus"), QStringLiteral("complete")},
                                               {QStringLiteral("inputTokenCount"), 2400},
                                               {QStringLiteral("outputTokenCount"), 999},
                                               {QStringLiteral("reasoningTokenCount"), 10000},
                                               {QStringLiteral("cacheCreationTokenCount"), 41},
                                               {QStringLiteral("cacheReadTokenCount"), 42},
                                               {QStringLiteral("totalTokenCount"), 13481},
                                               {QStringLiteral("durationMs"), 1250},
                                               {QStringLiteral("width"), 640}}));
    if (auto* item = qobject_cast<QQuickItem*>(object.get())) {
      window.resize(640, 480);
      item->setParentItem(window.contentItem());
      window.show();
      QTest::qWait(20);
    }
    return object;
  }

  QQmlEngine engine;
  QQuickWindow window;
};

TEST_F(ResponseStatsFooterQml, ShowsOnlyCompletedAssistantUsage) {
  auto footer = create();
  ASSERT_NE(footer, nullptr);
  EXPECT_TRUE(footer->property("visible").toBool());

  for (const auto& status : {"streaming", "cancelled", "error"}) {
    footer->setProperty("messageStatus", QString::fromLatin1(status));
    QCoreApplication::processEvents();
    EXPECT_FALSE(footer->property("visible").toBool()) << status;
  }

  footer->setProperty("messageStatus", QStringLiteral("complete"));
  footer->setProperty("durationMs", QVariant{});
  QCoreApplication::processEvents();
  EXPECT_FALSE(footer->property("visible").toBool());

  footer->setProperty("durationMs", 1250);
  footer->setProperty("isAssistant", false);
  QCoreApplication::processEvents();
  EXPECT_FALSE(footer->property("visible").toBool());
}

TEST_F(ResponseStatsFooterQml, FormatsBadgesAndKeepsOptionalBadgesIndependent) {
  auto footer = create();
  ASSERT_NE(footer, nullptr);
  auto* prompt = footer->findChild<QObject*>(QStringLiteral("promptTokenBadge"));
  auto* completion = footer->findChild<QObject*>(QStringLiteral("completionTokenBadge"));
  auto* reasoning = footer->findChild<QObject*>(QStringLiteral("reasoningTokenBadge"));
  auto* duration = footer->findChild<QObject*>(QStringLiteral("durationBadge"));
  ASSERT_NE(prompt, nullptr);
  ASSERT_NE(completion, nullptr);
  ASSERT_NE(reasoning, nullptr);
  ASSERT_NE(duration, nullptr);
  EXPECT_EQ(prompt->property("text").toString(), QStringLiteral("T 2.4K"));
  EXPECT_EQ(completion->property("text").toString(), QStringLiteral("C 999"));
  EXPECT_EQ(reasoning->property("text").toString(), QStringLiteral("R 10.0K"));
  EXPECT_EQ(duration->property("text").toString(), QStringLiteral("1.3s"));

  footer->setProperty("inputTokenCount", QVariant{});
  footer->setProperty("reasoningTokenCount", QVariant{});
  footer->setProperty("durationMs", 340);
  QCoreApplication::processEvents();
  EXPECT_FALSE(prompt->property("visible").toBool());
  EXPECT_TRUE(completion->property("visible").toBool());
  EXPECT_FALSE(reasoning->property("visible").toBool());
  EXPECT_EQ(duration->property("text").toString(), QStringLiteral("340ms"));
}

TEST_F(ResponseStatsFooterQml, ForwardsUsageAndTogglesPopupFromAccessibleButton) {
  auto footer = create();
  ASSERT_NE(footer, nullptr);
  auto* popup = footer->findChild<QObject*>(QStringLiteral("responseStatsPopup"));
  auto* button = footer->findChild<QObject*>(QStringLiteral("responseStatsInfoButton"));
  ASSERT_NE(popup, nullptr);
  ASSERT_NE(button, nullptr);

  EXPECT_EQ(popup->property("inputTokenCount").toInt(), 2400);
  EXPECT_EQ(popup->property("outputTokenCount").toInt(), 999);
  EXPECT_EQ(popup->property("reasoningTokenCount").toInt(), 10000);
  EXPECT_EQ(popup->property("cacheCreationTokenCount").toInt(), 41);
  EXPECT_EQ(popup->property("cacheReadTokenCount").toInt(), 42);
  EXPECT_EQ(popup->property("totalTokenCount").toInt(), 13481);
  EXPECT_EQ(popup->property("durationMs").toInt(), 1250);

  auto* accessible = QAccessible::queryAccessibleInterface(button);
  ASSERT_NE(accessible, nullptr);
  EXPECT_EQ(accessible->text(QAccessible::Name), QStringLiteral("View response stats"));

  EXPECT_FALSE(popup->property("visible").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  QTRY_VERIFY(popup->property("visible").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  QTRY_VERIFY(!popup->property("visible").toBool());
}

}  // namespace
