#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace {

std::unique_ptr<QObject> createDelegate(QQmlComponent& component, bool pinned) {
  QVariantMap model{{QStringLiteral("title"), QStringLiteral("Some title")}};
  return std::unique_ptr<QObject>(
      component.createWithInitialProperties({{QStringLiteral("model"), model},
                                             {QStringLiteral("conversationId"), QStringLiteral("conversation-a")},
                                             {QStringLiteral("updatedAt"), QStringLiteral("now")},
                                             {QStringLiteral("titleGenerationInProgress"), false},
                                             {QStringLiteral("pinned"), pinned}}));
}

TEST(ConversationListDelegateQml, NamingStatusTracksGenerationStateAndAccessibility) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/workspace/ConversationListDelegate.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(path));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  QVariantMap model{{QStringLiteral("title"), QStringLiteral("Fallback title")}};
  std::unique_ptr<QObject> delegate(
      component.createWithInitialProperties({{QStringLiteral("model"), model},
                                             {QStringLiteral("conversationId"), QStringLiteral("conversation-a")},
                                             {QStringLiteral("updatedAt"), QStringLiteral("now")},
                                             {QStringLiteral("titleGenerationInProgress"), false},
                                             {QStringLiteral("pinned"), false}}));
  ASSERT_NE(delegate, nullptr) << qPrintable(component.errorString());
  auto* timestamp = delegate->findChild<QQuickItem*>(QStringLiteral("updatedAtLabel"));
  auto* namingStatus = delegate->findChild<QQuickItem*>(QStringLiteral("namingStatus"));
  auto* namingLabel = delegate->findChild<QQuickItem*>(QStringLiteral("namingLabel"));
  ASSERT_NE(timestamp, nullptr);
  ASSERT_NE(namingStatus, nullptr);
  ASSERT_NE(namingLabel, nullptr);

  EXPECT_TRUE(timestamp->isVisible());
  EXPECT_EQ(namingStatus->opacity(), 0.0);
  auto* accessible = QAccessible::queryAccessibleInterface(delegate.get());
  ASSERT_NE(accessible, nullptr);
  EXPECT_EQ(accessible->text(QAccessible::Description), QString());

  delegate->setProperty("titleGenerationInProgress", true);
  QCoreApplication::processEvents();
  EXPECT_FALSE(timestamp->isVisible());
  QTRY_VERIFY(namingStatus->opacity() > 0.0);
  EXPECT_EQ(namingLabel->property("text").toString(), QStringLiteral("Naming…"));
  EXPECT_EQ(accessible->text(QAccessible::Description), QStringLiteral("Generating conversation title"));

  delegate->setProperty("titleGenerationInProgress", false);
  QCoreApplication::processEvents();
  EXPECT_TRUE(timestamp->isVisible());
  EXPECT_EQ(accessible->text(QAccessible::Description), QString());
}

TEST(ConversationListDelegateQml, PinMenuItemLabelTracksPinnedState) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/workspace/ConversationListDelegate.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(path));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> unpinnedDelegate = createDelegate(component, false);
  ASSERT_NE(unpinnedDelegate, nullptr) << qPrintable(component.errorString());
  auto* unpinnedMenuItem = unpinnedDelegate->findChild<QQuickItem*>(QStringLiteral("pinMenuItem"));
  ASSERT_NE(unpinnedMenuItem, nullptr);
  EXPECT_EQ(unpinnedMenuItem->property("text").toString(), QStringLiteral("Pin"));

  std::unique_ptr<QObject> pinnedDelegate = createDelegate(component, true);
  ASSERT_NE(pinnedDelegate, nullptr) << qPrintable(component.errorString());
  auto* pinnedMenuItem = pinnedDelegate->findChild<QQuickItem*>(QStringLiteral("pinMenuItem"));
  ASSERT_NE(pinnedMenuItem, nullptr);
  EXPECT_EQ(pinnedMenuItem->property("text").toString(), QStringLiteral("Unpin"));
}

TEST(ConversationListDelegateQml, PinMenuItemTriggeredEmitsPinToggleRequested) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/workspace/ConversationListDelegate.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(path));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> delegate = createDelegate(component, false);
  ASSERT_NE(delegate, nullptr) << qPrintable(component.errorString());
  auto* pinMenuItem = delegate->findChild<QQuickItem*>(QStringLiteral("pinMenuItem"));
  ASSERT_NE(pinMenuItem, nullptr);

  QSignalSpy toggleSpy(delegate.get(), SIGNAL(pinToggleRequested()));
  QMetaObject::invokeMethod(pinMenuItem, "click");
  QCoreApplication::processEvents();

  EXPECT_EQ(toggleSpy.count(), 1);
}

}  // namespace
