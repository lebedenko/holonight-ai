#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

TEST(MarkdownBlock, UnsupportedDisclosureContainerDoesNotHideContent) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  const QString source = QStringLiteral(R"(<details>
<summary><b>Configuration details</b></summary>

Content before the extracted fence.)");
  engine.rootContext()->setContextProperty(QStringLiteral("markdownSource"), source);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.MarkdownBlock {
          width: 420
          markdown: markdownSource
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/markdown-block-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  EXPECT_EQ(object->property("markdown").toString(), source);
  EXPECT_TRUE(object->property("renderedMarkdown").toString().contains(QStringLiteral("&lt;details&gt;")));

  QObject* body = object->findChild<QObject*>(QStringLiteral("markdownBlockBody"));
  ASSERT_NE(body, nullptr);
  EXPECT_TRUE(body->property("text").toString().contains(QStringLiteral("Configuration details")));
  EXPECT_TRUE(body->property("text").toString().contains(QStringLiteral("Content before the extracted fence.")));
}

TEST(MarkdownBlock, ClosingDisclosureTagDoesNotHideFollowingMarkdown) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  const QString source = QStringLiteral("</details>\n\n---\n\n## Footer\n\nTesting complete!");
  engine.rootContext()->setContextProperty(QStringLiteral("markdownSource"), source);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.MarkdownBlock {
          width: 420
          markdown: markdownSource
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/markdown-block-footer-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  EXPECT_EQ(object->property("markdown").toString(), source);

  QObject* body = object->findChild<QObject*>(QStringLiteral("markdownBlockBody"));
  ASSERT_NE(body, nullptr);
  EXPECT_TRUE(body->property("text").toString().contains(QStringLiteral("Footer")));
  EXPECT_TRUE(body->property("text").toString().contains(QStringLiteral("Testing complete!")));
}

}  // namespace
