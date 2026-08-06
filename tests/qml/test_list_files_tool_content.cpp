#include <QDir>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

std::unique_ptr<QObject> createContent(QQmlEngine& engine, const QVariantMap& detail_data, bool is_error = false) {
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ListFilesToolContent.qml"));
  QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
  if (!component.isReady()) {
    ADD_FAILURE() << qPrintable(component.errorString());
    return {};
  }

  QVariantMap tool_call;
  tool_call.insert(QStringLiteral("status"), is_error ? QStringLiteral("failed") : QStringLiteral("completed"));
  tool_call.insert(QStringLiteral("isError"), is_error);
  tool_call.insert(QStringLiteral("detailData"), detail_data);
  return std::unique_ptr<QObject>(component.createWithInitialProperties({{QStringLiteral("toolCall"), tool_call}}));
}

void configureEngine(QQmlEngine& engine) {
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));
}

TEST(ListFilesToolContentQml, ShowsPathCountsAndDeterministicEntryRows) {
  QQmlEngine engine;
  configureEngine(engine);
  const QVariantList entries = {
      QVariantMap{{QStringLiteral("name"), QStringLiteral("alpha")},
                  {QStringLiteral("kind"), QStringLiteral("directory")}},
      QVariantMap{{QStringLiteral("name"), QStringLiteral("beta.txt")},
                  {QStringLiteral("kind"), QStringLiteral("file")}},
  };
  auto content = createContent(engine, {{QStringLiteral("path"), QStringLiteral("/tmp/example")},
                                        {QStringLiteral("fileCount"), 1},
                                        {QStringLiteral("directoryCount"), 1},
                                        {QStringLiteral("entries"), entries}});
  ASSERT_NE(content, nullptr);

  auto* path = content->findChild<QObject*>(QStringLiteral("listFilesPath"));
  auto* metadata = content->findChild<QObject*>(QStringLiteral("listFilesMetadata"));
  auto* list = content->findChild<QObject*>(QStringLiteral("listFilesEntryList"));
  ASSERT_NE(path, nullptr);
  ASSERT_NE(metadata, nullptr);
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(path->property("text").toString(), QStringLiteral("Path: /tmp/example"));
  EXPECT_EQ(metadata->property("text").toString(), QStringLiteral("completed · 1 files · 1 folders"));
  EXPECT_EQ(list->property("count").toInt(), 2);
}

TEST(ListFilesToolContentQml, BoundsPreviewAndViewAllRevealsExistingEntries) {
  QQmlEngine engine;
  configureEngine(engine);
  QVariantList entries;
  for (int index = 0; index < 25; ++index) {
    entries.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("entry-%1").arg(index)},
                               {QStringLiteral("kind"), QStringLiteral("file")}});
  }
  auto content = createContent(
      engine,
      {{QStringLiteral("fileCount"), 25}, {QStringLiteral("directoryCount"), 0}, {QStringLiteral("entries"), entries}});
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->property("visibleEntryCount").toInt(), 20);

  auto* button = content->findChild<QObject*>(QStringLiteral("listFilesViewAll"));
  ASSERT_NE(button, nullptr);
  EXPECT_TRUE(button->property("visible").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  EXPECT_EQ(content->property("visibleEntryCount").toInt(), 25);
  EXPECT_FALSE(button->property("visible").toBool());
}

TEST(ListFilesToolContentQml, DistinguishesEmptyAndStructuredErrorStates) {
  QQmlEngine engine;
  configureEngine(engine);
  auto empty = createContent(engine, {{QStringLiteral("fileCount"), 0},
                                      {QStringLiteral("directoryCount"), 0},
                                      {QStringLiteral("entries"), QVariantList{}}});
  ASSERT_NE(empty, nullptr);
  EXPECT_TRUE(empty->findChild<QObject*>(QStringLiteral("listFilesEmptyState"))->property("visible").toBool());
  EXPECT_FALSE(empty->findChild<QObject*>(QStringLiteral("listFilesErrorState"))->property("visible").toBool());

  auto failure = createContent(engine,
                               {{QStringLiteral("entries"), QVariantList{}},
                                {QStringLiteral("errorCode"), QStringLiteral("ACCESS_DENIED")},
                                {QStringLiteral("errorMessage"), QStringLiteral("Permission denied")}},
                               true);
  ASSERT_NE(failure, nullptr);
  EXPECT_FALSE(failure->findChild<QObject*>(QStringLiteral("listFilesEmptyState"))->property("visible").toBool());
  EXPECT_TRUE(failure->findChild<QObject*>(QStringLiteral("listFilesErrorState"))->property("visible").toBool());
}

}  // namespace
