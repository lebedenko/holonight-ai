#include "holonight_application/message_content_model.h"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

TEST(MessageContentModel, TerminalSourcePublishesSynchronouslyAndRetainsRawMarkdown) {
  MessageContentModel model;
  const QString source = QStringLiteral("before\n\n```cpp\nint ніч = 1;\n```\n\nafter\n");

  model.setSource(source, true);

  EXPECT_EQ(model.rawMarkdown(), source);
  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.data(model.index(0), MessageContentModel::BlockIdRole).toString(), QStringLiteral("markdown:0"));
  EXPECT_EQ(model.data(model.index(1), MessageContentModel::TextRole).toString(), QStringLiteral("int ніч = 1;\n"));
  EXPECT_TRUE(model.data(model.index(1), MessageContentModel::CompleteRole).toBool());
}

TEST(MessageContentModel, StreamingUpdatesCoalesceAndUseLatestSource) {
  MessageContentModel model;
  QSignalSpy published(&model, &MessageContentModel::published);

  model.setSource(QStringLiteral("first"), false);
  model.setSource(QStringLiteral("latest"), false);

  EXPECT_EQ(model.rowCount(), 0);
  QTRY_COMPARE_WITH_TIMEOUT(published.count(), 1, 250);
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.data(model.index(0), MessageContentModel::TextRole).toString(), QStringLiteral("latest"));
  EXPECT_FALSE(model.data(model.index(0), MessageContentModel::CompleteRole).toBool());
}

TEST(MessageContentModel, TerminalFlushCancelsPendingStreamingPublication) {
  MessageContentModel model;
  QSignalSpy published(&model, &MessageContentModel::published);

  model.setSource(QStringLiteral("partial"), false);
  model.setSource(QStringLiteral("final"), true);

  ASSERT_EQ(published.count(), 1);
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.data(model.index(0), MessageContentModel::TextRole).toString(), QStringLiteral("final"));
  EXPECT_TRUE(model.data(model.index(0), MessageContentModel::CompleteRole).toBool());
  QTest::qWait(75);
  EXPECT_EQ(published.count(), 1);
}

TEST(MessageContentModel, MatchingTailUpdatesWithoutStructuralSignals) {
  MessageContentModel model;
  model.setSource(QStringLiteral("```cpp\npartial"), true);
  const QString block_id = model.data(model.index(0), MessageContentModel::BlockIdRole).toString();
  QSignalSpy rowsRemoved(&model, &QAbstractItemModel::rowsRemoved);
  QSignalSpy rowsInserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

  model.setSource(QStringLiteral("```cpp\ncomplete\n```"), true);

  EXPECT_EQ(model.data(model.index(0), MessageContentModel::BlockIdRole).toString(), block_id);
  EXPECT_EQ(rowsRemoved.count(), 0);
  EXPECT_EQ(rowsInserted.count(), 0);
  EXPECT_EQ(changed.count(), 1);
}

}  // namespace
}  // namespace holonight_application
