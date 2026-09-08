#include "qml/fakes/chat_view_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtQml>

#include <gtest/gtest.h>
#include <memory>

namespace {

void clickItem(QQuickWindow* window, QQuickItem* item) {
  const QPointF scene_position = item->mapToScene(QPointF{item->width() / 2, item->height() / 2});
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene_position.toPoint());
  QCoreApplication::processEvents();
}

void typeText(QQuickWindow* window, const QByteArray& text) {
  for (const char character : text) {
    QTest::keyClick(window, character);
  }
}

TEST(ChatComposerQml, DraftSynchronizationAndSubmissionPolicy) {
  FakeChatViewModel view_model;
  qmlRegisterSingletonInstance("HolonightChat", 1, 0, "ChatViewModel", &view_model);

  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));

  const QString composer_path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ChatComposer.qml"));
  QQmlComponent component{&engine, QUrl::fromLocalFile(composer_path)};
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> composer_object{component.create()};
  ASSERT_NE(composer_object, nullptr) << qPrintable(component.errorString());
  auto* composer = qobject_cast<QQuickItem*>(composer_object.get());
  ASSERT_NE(composer, nullptr);
  composer->setWidth(760);

  QQuickWindow window;
  window.resize(800, 400);
  composer->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

  auto* editor = composer->findChild<QQuickItem*>(QStringLiteral("composerEditor"));
  auto* actions = composer->findChild<QQuickItem*>(QStringLiteral("composerActions"));
  auto* send_button = composer->findChild<QQuickItem*>(QStringLiteral("sendButton"));
  ASSERT_NE(editor, nullptr);
  ASSERT_NE(actions, nullptr);
  ASSERT_NE(send_button, nullptr);

  QTRY_VERIFY(editor->hasActiveFocus());
  typeText(&window, QByteArrayLiteral("Hello"));
  QTRY_COMPARE(view_model.inputText(), QStringLiteral("Hello"));

  QTest::keyClick(&window, Qt::Key_Return);
  QTRY_COMPARE(view_model.sendCount(), 1);
  EXPECT_EQ(view_model.lastSentText(), QStringLiteral("Hello"));
  QTRY_COMPARE(editor->property("text").toString(), QString{});

  QTest::keyClick(&window, Qt::Key_Return);
  clickItem(&window, send_button);
  EXPECT_EQ(view_model.sendCount(), 1);

  typeText(&window, QByteArrayLiteral("First"));
  QTest::keyClick(&window, Qt::Key_Return, Qt::ShiftModifier);
  typeText(&window, QByteArrayLiteral("Second"));
  QTRY_COMPARE(view_model.inputText(), QStringLiteral("First\nSecond"));
  EXPECT_EQ(view_model.sendCount(), 1);

  view_model.setInputText(QStringLiteral("   "));
  QTest::keyClick(&window, Qt::Key_Return);
  EXPECT_EQ(view_model.sendCount(), 1);
  clickItem(&window, send_button);
  EXPECT_EQ(view_model.sendCount(), 1);

  view_model.setInputText(QStringLiteral("Blocked"));
  view_model.setIsStreaming(true);
  EXPECT_FALSE(send_button->property("enabled").toBool());
  QTest::keyClick(&window, Qt::Key_Return);
  clickItem(&window, send_button);
  EXPECT_EQ(view_model.sendCount(), 1);

  view_model.setIsStreaming(false);
  view_model.setCanSend(false);
  EXPECT_FALSE(send_button->property("enabled").toBool());
  QTest::keyClick(&window, Qt::Key_Return);
  clickItem(&window, send_button);
  EXPECT_EQ(view_model.sendCount(), 1);

  view_model.setCanSend(true);
  EXPECT_TRUE(send_button->property("enabled").toBool());
  clickItem(&window, send_button);
  QTRY_COMPARE(view_model.sendCount(), 2);
  EXPECT_EQ(view_model.lastSentText(), QStringLiteral("Blocked"));

  view_model.setInputText(QStringLiteral("Externally restored draft"));
  QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("Externally restored draft"));
}

}  // namespace
