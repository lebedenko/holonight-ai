#include <QClipboard>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace {

TEST(AssistantResponseContentQml, TerminalStatusesFlushAndCopyUntouchedSource) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QStringLiteral(QMARKDOWN_QML_IMPORT_PATH));
  const QString source = QString::fromUtf8("  intro & <tag> — café\n\n```txt\n a  b \n```\n");
  for (const auto& status : {QStringLiteral("complete"), QStringLiteral("error"), QStringLiteral("cancelled")}) {
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                                         QStringLiteral("/qml/shared/AssistantResponseContent.qml")));
    ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {QStringLiteral("width"), 640},
        {QStringLiteral("messageText"), source},
        {QStringLiteral("messageStatus"), QStringLiteral("streaming")},
    }));
    ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
    auto* copy = object->findChild<QObject*>(QStringLiteral("responseCopyButton"));
    auto* markdown = object->findChild<QObject*>(QStringLiteral("markdownBlockBody"));
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(markdown, nullptr);
    EXPECT_FALSE(copy->property("visible").toBool());
    EXPECT_TRUE(markdown->property("streamingEnabled").toBool());
    object->setProperty("messageStatus", status);
    QCoreApplication::processEvents();
    EXPECT_FALSE(markdown->property("streamingEnabled").toBool());
    EXPECT_EQ(markdown->property("markdown").toString(), source);
    EXPECT_TRUE(copy->property("visible").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(copy, "clicked"));
    EXPECT_EQ(QGuiApplication::clipboard()->text(), source);
    EXPECT_TRUE(copy->property("copied").toBool());
  }
}

}  // namespace
