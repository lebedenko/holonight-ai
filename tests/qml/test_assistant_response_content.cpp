#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QtQml>

#include <gtest/gtest.h>
#include <holonight_application/message_content_model.h>
#include <holonight_rendering/code_highlighter.h>
#include <holonight_rendering/content_block.h>
#include <memory>

namespace {

QObject* responseBlock(QObject* content, int index) {
  QVariant item;
  EXPECT_TRUE(QMetaObject::invokeMethod(content, "blockAt", Q_RETURN_ARG(QVariant, item), Q_ARG(int, index)));
  return item.value<QObject*>();
}

class AssistantResponseContentQml : public testing::Test {
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

  std::unique_ptr<QObject> create(holonight_application::MessageContentModel* model, const QString& status) {
    const QString path =
        QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/AssistantResponseContent.qml"));
    QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
    EXPECT_TRUE(component.isReady()) << qPrintable(component.errorString());
    if (!component.isReady()) {
      return {};
    }
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{QStringLiteral("width"), 640},
                                               {QStringLiteral("contentModel"), QVariant::fromValue(model)},
                                               {QStringLiteral("messageStatus"), status}}));
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

TEST_F(AssistantResponseContentQml, MixedBlocksStayOrderedAndCodeDelegateSurvivesCompletion) {
  holonight_application::MessageContentModel model;
  model.setSource(QStringLiteral("before\n\n```cpp\nint value = 1;"), false);
  QTest::qWait(75);
  ASSERT_EQ(model.rowCount(), 2);

  auto content = create(&model, QStringLiteral("streaming"));
  ASSERT_NE(content, nullptr);
  QCoreApplication::processEvents();
  auto* repeater = content->findChild<QObject*>(QStringLiteral("assistantResponseBlockRepeater"));
  ASSERT_NE(repeater, nullptr);
  ASSERT_EQ(repeater->property("count").toInt(), 2);
  auto* markdown_loader = responseBlock(content.get(), 0);
  auto* code_loader = responseBlock(content.get(), 1);
  ASSERT_NE(markdown_loader, nullptr);
  ASSERT_NE(code_loader, nullptr);
  EXPECT_EQ(markdown_loader->property("blockId").toString(), QStringLiteral("markdown:0"));
  EXPECT_EQ(code_loader->property("blockId").toString(), QStringLiteral("code:8"));
  auto* code = code_loader->property("item").value<QObject*>();
  ASSERT_NE(code, nullptr);
  EXPECT_EQ(code->property("highlighter").value<QObject*>(), nullptr);
  auto* body = code->findChild<QObject*>(QStringLiteral("codeBlockBody"));
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(body->property("text").toString(), QStringLiteral("int value = 1;"));
  EXPECT_TRUE(body->property("selectByMouse").toBool());

  model.setSource(QStringLiteral("before\n\n```cpp\nint value = 1;\n```\n\nafter"), true);
  QCoreApplication::processEvents();
  ASSERT_EQ(model.rowCount(), 3);
  ASSERT_EQ(repeater->property("count").toInt(), 3);
  EXPECT_EQ(responseBlock(content.get(), 1), code_loader);
  EXPECT_EQ(code_loader->property("item").value<QObject*>(), code);
  auto* highlighter_loader = code->findChild<QObject*>(QStringLiteral("codeHighlighterLoader"));
  ASSERT_NE(highlighter_loader, nullptr);
  ASSERT_NE(highlighter_loader->property("item").value<QObject*>(), nullptr);
}

TEST_F(AssistantResponseContentQml, TerminalCopyUsesExactRawMarkdownAndStreamingHidesAction) {
  const QString source = QString::fromUtf8("  intro & <tag> — ніч\n\n```txt\n a  b \n```\n");
  holonight_application::MessageContentModel model;
  model.setSource(source, true);
  auto content = create(&model, QStringLiteral("streaming"));
  ASSERT_NE(content, nullptr);

  auto* copy = content->findChild<QObject*>(QStringLiteral("responseCopyButton"));
  ASSERT_NE(copy, nullptr);
  EXPECT_FALSE(copy->property("visible").toBool());
  content->setProperty("messageStatus", QStringLiteral("cancelled"));
  QCoreApplication::processEvents();
  EXPECT_TRUE(copy->property("visible").toBool());

  QGuiApplication::clipboard()->clear();
  ASSERT_TRUE(QMetaObject::invokeMethod(copy, "clicked"));
  EXPECT_EQ(QGuiApplication::clipboard()->text(), source);
  EXPECT_TRUE(copy->property("copied").toBool());
}

TEST_F(AssistantResponseContentQml, CompletedUnknownLanguageRemainsPlainText) {
  holonight_application::MessageContentModel model;
  model.setSource(QStringLiteral("```definitely-unknown\nliteral <>&\n```"), true);
  auto content = create(&model, QStringLiteral("complete"));
  ASSERT_NE(content, nullptr);

  QCoreApplication::processEvents();
  auto* repeater = content->findChild<QObject*>(QStringLiteral("assistantResponseBlockRepeater"));
  ASSERT_NE(repeater, nullptr);
  auto* code_loader = responseBlock(content.get(), 0);
  ASSERT_NE(code_loader, nullptr);
  auto* code = code_loader->property("item").value<QObject*>();
  ASSERT_NE(code, nullptr);
  auto* highlighter_loader = code->findChild<QObject*>(QStringLiteral("codeHighlighterLoader"));
  ASSERT_NE(highlighter_loader, nullptr);
  ASSERT_NE(highlighter_loader->property("item").value<QObject*>(), nullptr);
  auto* label = code->findChild<QObject*>(QStringLiteral("codeLanguageLabel"));
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->property("text").toString(), QStringLiteral("plain text"));
}

}  // namespace
