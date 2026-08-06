#include "holonight_application/conversation_list_model.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QString>

#include <gtest/gtest.h>
#include <holonight_persistence/conversation_record.h>

namespace holonight_application {
namespace {

using holonight_persistence::ConversationSummary;

ConversationSummary makeSummary(const QString& conversationId, const QString& title, const QDateTime& updatedAt) {
  return ConversationSummary{
      .id = conversationId,
      .title = title,
      .created_at = updatedAt,
      .updated_at = updatedAt,
      .last_model_id = std::nullopt,
  };
}

QString idAt(const ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::IdRole).toString();
}

QString titleAt(const ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::TitleRole).toString();
}

QString updatedAtAt(const ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::UpdatedAtRole).toString();
}

bool titleGenerationInProgressAt(const ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::TitleGenerationInProgressRole).toBool();
}

bool pinnedAt(const ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::PinnedRole).toBool();
}

TEST(ConversationListModel, StartsEmpty) {
  ConversationListModel model;
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_TRUE(model.isEmpty());
  EXPECT_FALSE(model.firstConversationId().has_value());
}

TEST(ConversationListModel, SetAllPopulatesModelAndEmitsReset) {
  ConversationListModel model;
  QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});

  EXPECT_EQ(resetSpy.count(), 1);
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("a"));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("b"));
  EXPECT_EQ(model.firstConversationId(), QStringLiteral("a"));
  EXPECT_FALSE(model.isEmpty());
}

TEST(ConversationListModel, UpsertToFrontInsertsNewConversationAtRowZero) {
  ConversationListModel model;
  QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);

  model.upsertToFront(makeSummary(QStringLiteral("a"), QStringLiteral("A"), QDateTime::currentDateTimeUtc()));

  EXPECT_EQ(insertSpy.count(), 1);
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("a"));
}

TEST(ConversationListModel, UpsertToFrontMovesExistingConversationToFront) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});

  QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
  model.upsertToFront(makeSummary(QStringLiteral("b"), QStringLiteral("B renamed"), now.addSecs(5)));

  EXPECT_EQ(moveSpy.count(), 1);
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("b"));
  EXPECT_EQ(titleAt(model, 0), QStringLiteral("B renamed"));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("a"));
}

TEST(ConversationListModel, TouchToFrontPreservesTitleAndUpdatesTimestamp) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});

  const QDateTime touchedAt = now.addSecs(20);
  model.touchToFront(QStringLiteral("b"), touchedAt);

  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("b"));
  EXPECT_EQ(titleAt(model, 0), QStringLiteral("B"));
}

TEST(ConversationListModel, RemoveByIdRemovesRow) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});

  QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
  model.removeById(QStringLiteral("a"));

  EXPECT_EQ(removeSpy.count(), 1);
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("b"));
}

TEST(ConversationListModel, RoleNamesExposeExpectedRoles) {
  ConversationListModel model;
  const auto roles = model.roleNames();

  EXPECT_EQ(roles.value(ConversationListModel::IdRole), QByteArrayLiteral("conversationId"));
  EXPECT_EQ(roles.value(ConversationListModel::TitleRole), QByteArrayLiteral("title"));
  EXPECT_EQ(roles.value(ConversationListModel::UpdatedAtRole), QByteArrayLiteral("updatedAt"));
  EXPECT_EQ(roles.value(ConversationListModel::TitleGenerationInProgressRole),
            QByteArrayLiteral("titleGenerationInProgress"));
}

TEST(ConversationListModel, TitleGenerationProgressUpdatesOnlyMatchingRowAndRole) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});
  QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

  EXPECT_FALSE(titleGenerationInProgressAt(model, 0));
  model.setTitleGenerationInProgress(QStringLiteral("b"), true);

  ASSERT_EQ(changedSpy.count(), 1);
  EXPECT_EQ(changedSpy.at(0).at(0).toModelIndex().row(), 1);
  EXPECT_EQ(changedSpy.at(0).at(2).value<QList<int>>(),
            QList<int>{ConversationListModel::TitleGenerationInProgressRole});
  EXPECT_TRUE(titleGenerationInProgressAt(model, 1));

  model.setTitleGenerationInProgress(QStringLiteral("missing"), true);
  model.setTitleGenerationInProgress(QStringLiteral("b"), true);
  EXPECT_EQ(changedSpy.count(), 1);
}

TEST(ConversationListModel, TitleGenerationProgressSurvivesUpsertMoveAndReset) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});
  model.setTitleGenerationInProgress(QStringLiteral("b"), true);

  model.upsertToFront(makeSummary(QStringLiteral("b"), QStringLiteral("Renamed"), now.addSecs(10)));
  EXPECT_TRUE(titleGenerationInProgressAt(model, 0));

  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("Renamed"), now.addSecs(10))});
  EXPECT_TRUE(titleGenerationInProgressAt(model, 1));
}

TEST(ConversationListModel, PinUnpinReorder) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now.addSecs(-5)),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10)),
                makeSummary(QStringLiteral("c"), QStringLiteral("C"), now.addSecs(-15)),
                makeSummary(QStringLiteral("d"), QStringLiteral("D"), now.addSecs(-20)),
                makeSummary(QStringLiteral("e"), QStringLiteral("E"), now.addSecs(-25))});

  model.markPinned(QStringLiteral("a"), now.addSecs(-3));
  model.markPinned(QStringLiteral("b"), now.addSecs(-2));
  model.markPinned(QStringLiteral("c"), now.addSecs(-1));

  ASSERT_EQ(model.rowCount(), 5);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("c"));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("b"));
  EXPECT_EQ(idAt(model, 2), QStringLiteral("a"));
  EXPECT_EQ(idAt(model, 3), QStringLiteral("d"));
  EXPECT_EQ(idAt(model, 4), QStringLiteral("e"));

  QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
  model.markUnpinned(QStringLiteral("b"));

  ASSERT_EQ(moveSpy.count(), 1);
  const QList<QVariant>& moveArgs = moveSpy.at(0);
  EXPECT_EQ(moveArgs.at(1).toInt(), 1);  // sourceStart — b was at index 1
  EXPECT_EQ(moveArgs.at(2).toInt(), 1);  // sourceEnd
  EXPECT_EQ(moveArgs.at(4).toInt(), 3);  // destinationRow, pre-removal index space (d's original index)

  ASSERT_EQ(model.rowCount(), 5);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("c"));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("a"));
  EXPECT_EQ(idAt(model, 2), QStringLiteral("b"));
  EXPECT_EQ(idAt(model, 3), QStringLiteral("d"));
  EXPECT_EQ(idAt(model, 4), QStringLiteral("e"));
  EXPECT_TRUE(pinnedAt(model, 0));
  EXPECT_TRUE(pinnedAt(model, 1));
  EXPECT_FALSE(pinnedAt(model, 2));
  EXPECT_FALSE(pinnedAt(model, 3));
  EXPECT_FALSE(pinnedAt(model, 4));
}

TEST(ConversationListModel, UpsertToFrontDoesNotMovePinnedRow) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});
  model.markPinned(QStringLiteral("b"), now.addSecs(-1));
  ASSERT_EQ(idAt(model, 0), QStringLiteral("b"));
  ASSERT_EQ(idAt(model, 1), QStringLiteral("a"));

  QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
  model.upsertToFront(makeSummary(QStringLiteral("b"), QStringLiteral("B renamed"), now.addSecs(20)));

  EXPECT_EQ(moveSpy.count(), 0);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("b"));
  EXPECT_EQ(titleAt(model, 0), QStringLiteral("B renamed"));
  EXPECT_TRUE(pinnedAt(model, 0));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("a"));
}

TEST(ConversationListModel, UpsertToFrontInsertsNewConversationAfterPinnedBlock) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now)});
  model.markPinned(QStringLiteral("a"), now);

  model.upsertToFront(makeSummary(QStringLiteral("new"), QStringLiteral("New"), now.addSecs(5)));

  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("a"));
  EXPECT_TRUE(pinnedAt(model, 0));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("new"));
  EXPECT_FALSE(pinnedAt(model, 1));
}

TEST(ConversationListModel, TouchToFrontDoesNotMovePinnedRow) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("a"), QStringLiteral("A"), now),
                makeSummary(QStringLiteral("b"), QStringLiteral("B"), now.addSecs(-10))});
  model.markPinned(QStringLiteral("b"), now.addSecs(-1));

  QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
  model.touchToFront(QStringLiteral("b"), now.addSecs(30));

  EXPECT_EQ(moveSpy.count(), 0);
  EXPECT_EQ(idAt(model, 0), QStringLiteral("b"));
  EXPECT_EQ(idAt(model, 1), QStringLiteral("a"));
}

TEST(ConversationListModel, FormatsUpdatedAtAsCompactRelativeTime) {
  ConversationListModel model;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  model.setAll({makeSummary(QStringLiteral("now"), QStringLiteral("Now"), now),
                makeSummary(QStringLiteral("minutes"), QStringLiteral("Minutes"), now.addSecs(-2 * 60)),
                makeSummary(QStringLiteral("hours"), QStringLiteral("Hours"), now.addSecs(-3 * 60 * 60)),
                makeSummary(QStringLiteral("days"), QStringLiteral("Days"), now.addDays(-4))});

  EXPECT_EQ(updatedAtAt(model, 0), QStringLiteral("now"));
  EXPECT_EQ(updatedAtAt(model, 1), QStringLiteral("2m ago"));
  EXPECT_EQ(updatedAtAt(model, 2), QStringLiteral("3h ago"));
  EXPECT_EQ(updatedAtAt(model, 3), QStringLiteral("4d ago"));
}

}  // namespace
}  // namespace holonight_application
