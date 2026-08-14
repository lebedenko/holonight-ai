#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QtQml>

#include <gtest/gtest.h>
#include <holonight_application/message_content_model.h>
#include <holonight_rendering/code_highlighter.h>
#include <holonight_rendering/content_block.h>
#include <memory>

namespace {

QVariantMap runningToolCall() {
  return {{QStringLiteral("toolUseId"), QStringLiteral("tool-42")},
          {QStringLiteral("kind"), QStringLiteral("invocation")},
          {QStringLiteral("status"), QStringLiteral("running")},
          {QStringLiteral("canCancel"), true},
          {QStringLiteral("functionName"), QStringLiteral("list_files")},
          {QStringLiteral("rendererKey"), QStringLiteral("generic")},
          {QStringLiteral("toolTitle"), QStringLiteral("List files")},
          {QStringLiteral("summary"), QStringLiteral("Working")}};
}

class ChatMessageDelegateQml : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    qmlRegisterTypesAndRevisions<holonight_rendering::CodeHighlighter>("HolonightChat", 1);
    qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlock>("HolonightChat", 1);
    qmlRegisterTypesAndRevisions<holonight_rendering::ContentBlockTypeNs>("HolonightChat", 1);
  }

  void SetUp() override {
    engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
    engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
  }

  std::unique_ptr<QObject> create(const QString& role, const QVariant& toolCall = {}) {
    const QString path =
        QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ChatMessageDelegate.qml"));
    QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
    EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
    if (!component.isReady()) {
      return {};
    }

    return std::unique_ptr<QObject>(component.createWithInitialProperties(
        {{QStringLiteral("messageId"), QStringLiteral("message-1")},
         {QStringLiteral("role"), role},
         {QStringLiteral("text"), QStringLiteral("Hello")},
         {QStringLiteral("status"), QStringLiteral("streaming")},
         {QStringLiteral("createdAt"), QDateTime::fromString(QStringLiteral("2026-08-06T12:00:00Z"), Qt::ISODate)},
         {QStringLiteral("modelName"), QStringLiteral("model-a")},
         {QStringLiteral("providerId"), QStringLiteral("provider-id")},
         {QStringLiteral("providerType"), QStringLiteral("openai")},
         {QStringLiteral("providerName"), QStringLiteral("Provider A")},
         {QStringLiteral("contentBlocks"), QVariant::fromValue(&content_model)},
         {QStringLiteral("inputTokenCount"), 11},
         {QStringLiteral("outputTokenCount"), 22},
         {QStringLiteral("reasoningTokenCount"), 3},
         {QStringLiteral("cacheCreationTokenCount"), 4},
         {QStringLiteral("cacheReadTokenCount"), 5},
         {QStringLiteral("totalTokenCount"), 45},
         {QStringLiteral("durationMs"), 678},
         {QStringLiteral("toolCall"), toolCall},
         {QStringLiteral("messageWidthRatio"), 0.6},
         {QStringLiteral("width"), 800}}));
  }

  QQmlEngine engine;
  holonight_application::MessageContentModel content_model;
};

TEST_F(ChatMessageDelegateQml, RoutesUserAssistantFallbackAndToolRowsWithExistingPrecedence) {
  auto user = create(QStringLiteral("user"));
  ASSERT_NE(user, nullptr);
  EXPECT_NE(user->findChild<QObject*>(QStringLiteral("chatUserMessageCard")), nullptr);

  auto nullTool = create(QStringLiteral("user"), QVariant::fromValue(static_cast<QObject*>(nullptr)));
  ASSERT_NE(nullTool, nullptr);
  EXPECT_NE(nullTool->findChild<QObject*>(QStringLiteral("chatUserMessageCard")), nullptr);

  auto assistant = create(QStringLiteral("assistant"));
  ASSERT_NE(assistant, nullptr);
  EXPECT_NE(assistant->findChild<QObject*>(QStringLiteral("chatMessageBubble")), nullptr);

  auto system = create(QStringLiteral("system"));
  ASSERT_NE(system, nullptr);
  EXPECT_NE(system->findChild<QObject*>(QStringLiteral("chatMessageBubble")), nullptr);

  auto tool = create(QStringLiteral("user"), runningToolCall());
  ASSERT_NE(tool, nullptr);
  EXPECT_NE(tool->findChild<QObject*>(QStringLiteral("chatToolActivityCard")), nullptr);
  EXPECT_EQ(tool->findChild<QObject*>(QStringLiteral("chatUserMessageCard")), nullptr);
}

TEST_F(ChatMessageDelegateQml, PreservesBindingsLayoutAndLoadedCardIdentity) {
  auto delegate = create(QStringLiteral("assistant"));
  ASSERT_NE(delegate, nullptr);
  QCoreApplication::processEvents();

  auto* loader = delegate->findChild<QObject*>(QStringLiteral("chatMessageLoader"));
  auto* bubble = delegate->findChild<QObject*>(QStringLiteral("chatMessageBubble"));
  ASSERT_NE(loader, nullptr);
  ASSERT_NE(bubble, nullptr);
  EXPECT_TRUE(loader->property("active").toBool());
  EXPECT_DOUBLE_EQ(bubble->property("width").toDouble(), 800.0);
  EXPECT_DOUBLE_EQ(bubble->property("maximumWidthRatio").toDouble(), 0.6);
  EXPECT_EQ(bubble->property("messageRole").toString(), QStringLiteral("assistant"));
  EXPECT_EQ(bubble->property("messageText").toString(), QStringLiteral("Hello"));
  EXPECT_EQ(bubble->property("messageStatus").toString(), QStringLiteral("streaming"));
  EXPECT_EQ(bubble->property("modelName").toString(), QStringLiteral("model-a"));
  EXPECT_EQ(bubble->property("providerId").toString(), QStringLiteral("provider-id"));
  EXPECT_EQ(bubble->property("providerType").toString(), QStringLiteral("openai"));
  EXPECT_EQ(bubble->property("providerName").toString(), QStringLiteral("Provider A"));
  EXPECT_EQ(bubble->property("contentBlocks").value<QObject*>(), &content_model);
  EXPECT_EQ(bubble->property("inputTokenCount").toInt(), 11);
  EXPECT_EQ(bubble->property("outputTokenCount").toInt(), 22);
  EXPECT_EQ(bubble->property("reasoningTokenCount").toInt(), 3);
  EXPECT_EQ(bubble->property("cacheCreationTokenCount").toInt(), 4);
  EXPECT_EQ(bubble->property("cacheReadTokenCount").toInt(), 5);
  EXPECT_EQ(bubble->property("totalTokenCount").toInt(), 45);
  EXPECT_EQ(bubble->property("durationMs").toInt(), 678);
  EXPECT_DOUBLE_EQ(delegate->property("height").toDouble(), bubble->property("implicitHeight").toDouble());

  delegate->setProperty("role", QStringLiteral("system"));
  delegate->setProperty("text", QStringLiteral("Updated"));
  delegate->setProperty("providerName", QStringLiteral("Provider B"));
  delegate->setProperty("outputTokenCount", 29);
  QCoreApplication::processEvents();
  EXPECT_EQ(delegate->findChild<QObject*>(QStringLiteral("chatMessageBubble")), bubble);
  EXPECT_EQ(bubble->property("messageRole").toString(), QStringLiteral("system"));
  EXPECT_EQ(bubble->property("messageText").toString(), QStringLiteral("Updated"));
  EXPECT_EQ(bubble->property("providerName").toString(), QStringLiteral("Provider B"));
  EXPECT_EQ(bubble->property("outputTokenCount").toInt(), 29);

  delegate->setProperty("width", 0);
  QCoreApplication::processEvents();
  EXPECT_FALSE(loader->property("active").toBool());
  EXPECT_EQ(loader->property("item").value<QObject*>(), nullptr);
  EXPECT_DOUBLE_EQ(delegate->property("height").toDouble(), 0.0);
}

TEST_F(ChatMessageDelegateQml, ForwardsToolStopIdAndExpansionLifecycle) {
  auto delegate = create(QStringLiteral("assistant"), runningToolCall());
  ASSERT_NE(delegate, nullptr);
  auto* stop = delegate->findChild<QObject*>(QStringLiteral("toolActivityStop"));
  auto* disclosure = delegate->findChild<QObject*>(QStringLiteral("toolActivityDisclosure"));
  ASSERT_NE(stop, nullptr);
  ASSERT_NE(disclosure, nullptr);

  QSignalSpy stopSpy(delegate.get(), SIGNAL(stopRequested(QString)));
  QSignalSpy startedSpy(delegate.get(), SIGNAL(userExpansionStarted()));
  QSignalSpy finishedSpy(delegate.get(), SIGNAL(userExpansionFinished()));
  ASSERT_TRUE(stopSpy.isValid());
  ASSERT_TRUE(startedSpy.isValid());
  ASSERT_TRUE(finishedSpy.isValid());

  ASSERT_TRUE(QMetaObject::invokeMethod(stop, "clicked"));
  ASSERT_EQ(stopSpy.count(), 1);
  EXPECT_EQ(stopSpy.takeFirst().at(0).toString(), QStringLiteral("tool-42"));

  ASSERT_TRUE(QMetaObject::invokeMethod(disclosure, "clicked"));
  EXPECT_EQ(startedSpy.count(), 1);
  QTRY_COMPARE(finishedSpy.count(), 1);
}

}  // namespace
