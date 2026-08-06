#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
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
  auto actions = create();
  ASSERT_NE(actions, nullptr);
  EXPECT_EQ(actions->objectName(), QStringLiteral("composerActions"));

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
  EXPECT_TRUE(QQmlProperty{attachment, QStringLiteral("foregroundColor")}.isValid());
  EXPECT_TRUE(QQmlProperty{context, QStringLiteral("foregroundColor")}.isValid());
  EXPECT_TRUE(QQmlProperty{tools, QStringLiteral("foregroundColor")}.isValid());
  EXPECT_EQ(attachment->implicitHeight(), context->implicitHeight());
  EXPECT_EQ(attachment->implicitHeight(), tools->implicitHeight());
  EXPECT_EQ(attachment->implicitWidth(), attachment->implicitHeight());
  EXPECT_EQ(iconSource(attachment), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/paperclip.svg")});
  EXPECT_EQ(iconSource(context), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/folder.svg")});
  EXPECT_EQ(iconSource(tools), QUrl{QStringLiteral("qrc:/qt/qml/Holonight/Controls/assets/wrench.svg")});

  actions->setProperty("compact", true);
  QCoreApplication::processEvents();
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
  EXPECT_TRUE(QQmlProperty{send, QStringLiteral("foregroundColor")}.isValid());
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
