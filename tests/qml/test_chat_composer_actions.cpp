#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

class ChatComposerActionsQml : public testing::Test {
 protected:
  void SetUp() override {
    engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  }

  std::unique_ptr<QObject> create() {
    const QString path =
        QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ChatComposerActions.qml"));
    QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
    EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
    if (!component.isReady()) {
      return {};
    }

    return std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("compact"), false},
                                                                           {QStringLiteral("showRetryAction"), true},
                                                                           {QStringLiteral("canRegenerate"), true},
                                                                           {QStringLiteral("canSubmit"), true},
                                                                           {QStringLiteral("width"), 760}}));
  }

  static QUrl iconSource(QObject* button) { return QQmlProperty::read(button, QStringLiteral("icon.source")).toUrl(); }

  QQmlEngine engine;
};

TEST_F(ChatComposerActionsQml, PreservesDesktopAndCompactPresentation) {
  QQuickWindow window;
  window.resize(800, 100);
  auto actions = create();
  ASSERT_NE(actions, nullptr);
  EXPECT_EQ(actions->objectName(), QStringLiteral("composerActions"));
  auto* row = qobject_cast<QQuickItem*>(actions.get());
  ASSERT_NE(row, nullptr);
  row->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
  for (auto* child : row->childItems()) {
    child->ensurePolished();
  }
  row->ensurePolished();
  // Native font metrics can require more space than the fixture's default width.
  row->setWidth(qMax(row->width(), row->implicitWidth()));
  window.setWidth(qRound(row->width()));
  row->ensurePolished();

  auto* attachment = actions->findChild<QQuickItem*>(QStringLiteral("attachmentButton"));
  auto* context = actions->findChild<QQuickItem*>(QStringLiteral("contextButton"));
  auto* tools = actions->findChild<QQuickItem*>(QStringLiteral("toolsButton"));
  auto* hint = actions->findChild<QQuickItem*>(QStringLiteral("composerHint"));
  ASSERT_NE(attachment, nullptr);
  ASSERT_NE(context, nullptr);
  ASSERT_NE(tools, nullptr);
  ASSERT_NE(hint, nullptr);

  EXPECT_TRUE(attachment->property("visible").toBool());
  EXPECT_TRUE(context->property("visible").toBool());
  EXPECT_TRUE(tools->property("visible").toBool());
  EXPECT_TRUE(hint->property("visible").toBool());
  EXPECT_FALSE(attachment->property("enabled").toBool());
  EXPECT_FALSE(context->property("enabled").toBool());
  EXPECT_FALSE(tools->property("enabled").toBool());
  EXPECT_TRUE((QQmlProperty{attachment, QStringLiteral("icon.color")}.isValid()));
  EXPECT_TRUE((QQmlProperty{context, QStringLiteral("icon.color")}.isValid()));
  EXPECT_TRUE((QQmlProperty{tools, QStringLiteral("icon.color")}.isValid()));
  auto* background = attachment->property("background").value<QObject*>();
  ASSERT_NE(background, nullptr);
  auto* background_context = qmlContext(background);
  ASSERT_NE(background_context, nullptr);
  if (background_context->baseUrl().toString().contains(QStringLiteral("/Holonight/"))) {
    EXPECT_EQ(attachment->implicitHeight(), context->implicitHeight());
    EXPECT_EQ(attachment->implicitHeight(), tools->implicitHeight());
    EXPECT_EQ(attachment->height(), context->height());
    EXPECT_EQ(attachment->height(), tools->height());
  }
  EXPECT_GT(row->width(), 0);
  EXPECT_GT(row->height(), 0);
  qreal previous_right = 0;
  for (const auto* name :
       {"attachmentButton", "contextButton", "toolsButton", "retryButton", "composerHint", "sendButton"}) {
    auto* control = row->findChild<QQuickItem*>(QString::fromLatin1(name));
    ASSERT_NE(control, nullptr) << name;
    SCOPED_TRACE(name);
    EXPECT_GT(control->implicitWidth(), 0);
    EXPECT_GT(control->implicitHeight(), 0);
    EXPECT_GT(control->width(), 0);
    EXPECT_GT(control->height(), 0);
    EXPECT_GE(control->x(), previous_right);
    EXPECT_GE(control->y(), 0);
    EXPECT_LE(control->x() + control->width(), row->width());
    EXPECT_LE(control->y() + control->height(), row->height());
    EXPECT_NEAR(control->y() + (control->height() / 2), row->height() / 2, 1);
    previous_right = control->x() + control->width();
  }
  EXPECT_EQ(attachment->width(), attachment->height());
  EXPECT_EQ(attachment->implicitWidth(), attachment->implicitHeight());
  EXPECT_EQ(iconSource(attachment), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/paperclip.svg")});
  EXPECT_EQ(iconSource(context), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/folder.svg")});
  EXPECT_EQ(iconSource(tools), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/wrench.svg")});

  actions->setProperty("compact", true);
  QCoreApplication::processEvents();
  row->ensurePolished();
  EXPECT_FALSE(attachment->property("visible").toBool());
  EXPECT_FALSE(context->property("visible").toBool());
  EXPECT_FALSE(tools->property("visible").toBool());
  EXPECT_FALSE(hint->property("visible").toBool());
}

TEST_F(ChatComposerActionsQml, PreservesPlaceholderAccessibilityDescriptions) {
  auto actions = create();
  ASSERT_NE(actions, nullptr);

  const QList<QPair<QString, QString>> descriptions = {
      {QStringLiteral("attachmentButton"), QStringLiteral("File attachments are not available yet")},
      {QStringLiteral("contextButton"), QStringLiteral("Context folder selection is not available yet")},
      {QStringLiteral("toolsButton"), QStringLiteral("Tools are not available yet")},
  };
  for (const auto& [object_name, expected_description] : descriptions) {
    auto* button = actions->findChild<QObject*>(object_name);
    ASSERT_NE(button, nullptr) << qPrintable(object_name);
    auto* accessible = QAccessible::queryAccessibleInterface(button);
    ASSERT_NE(accessible, nullptr) << qPrintable(object_name);
    EXPECT_EQ(accessible->text(QAccessible::Description), expected_description);
  }
}

TEST_F(ChatComposerActionsQml, AppliesRetryPolicyAndForwardsRequest) {
  auto actions = create();
  ASSERT_NE(actions, nullptr);
  auto* retry = actions->findChild<QObject*>(QStringLiteral("retryButton"));
  ASSERT_NE(retry, nullptr);
  EXPECT_TRUE(retry->property("visible").toBool());

  actions->setProperty("showRetryAction", false);
  EXPECT_FALSE(retry->property("visible").toBool());
  actions->setProperty("showRetryAction", true);
  actions->setProperty("canRegenerate", false);
  EXPECT_FALSE(retry->property("visible").toBool());
  actions->setProperty("canRegenerate", true);
  EXPECT_TRUE(retry->property("visible").toBool());

  QSignalSpy retry_spy(actions.get(), SIGNAL(retryRequested()));
  ASSERT_TRUE(QMetaObject::invokeMethod(retry, "clicked"));
  EXPECT_EQ(retry_spy.count(), 1);
}

TEST_F(ChatComposerActionsQml, AppliesSubmitPolicyAndForwardsRequest) {
  auto actions = create();
  ASSERT_NE(actions, nullptr);
  auto* send = actions->findChild<QObject*>(QStringLiteral("sendButton"));
  ASSERT_NE(send, nullptr);
  EXPECT_TRUE((QQmlProperty{send, QStringLiteral("icon.color")}.isValid()));
  EXPECT_TRUE(send->property("enabled").toBool());
  EXPECT_EQ(iconSource(send), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/send.svg")});

  actions->setProperty("canSubmit", false);
  EXPECT_FALSE(send->property("enabled").toBool());
  actions->setProperty("canSubmit", true);
  EXPECT_TRUE(send->property("enabled").toBool());

  QSignalSpy submit_spy(actions.get(), SIGNAL(submitRequested()));
  ASSERT_TRUE(QMetaObject::invokeMethod(send, "clicked"));
  EXPECT_EQ(submit_spy.count(), 1);
}

}  // namespace
