#include "holonight_application/conversation_list_model.h"

#include <QModelIndex>
#include <QVariant>

#include <algorithm>
#include <chrono>
#include <iterator>

namespace holonight_application {

using holonight_domain::TitleSource;
using holonight_persistence::ConversationSummary;

namespace {

QString titleSourceToString(TitleSource source) {
  switch (source) {
    case TitleSource::Fallback:
      return QStringLiteral("fallback");
    case TitleSource::Generated:
      return QStringLiteral("generated");
    case TitleSource::Manual:
      return QStringLiteral("manual");
  }
  return QStringLiteral("unknown");
}

}  // namespace

ConversationListModel::ConversationListModel(QObject* parent) : QAbstractListModel(parent) {
  relative_time_refresh_timer_.setInterval(std::chrono::minutes(1));
  connect(&relative_time_refresh_timer_, &QTimer::timeout, this, [this] {
    if (!rows_.empty()) {
      emit dataChanged(index(0), index(rowCount() - 1), {UpdatedAtRole});
    }
  });
  relative_time_refresh_timer_.start();
}

int ConversationListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return static_cast<int>(rows_.size());
}

QVariant ConversationListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
    return {};
  }

  const Row& row = rows_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case IdRole:
      return row.id;
    case TitleRole:
      return row.title;
    case UpdatedAtRole:
      return formatRelativeTime(row.updated_at, QDateTime::currentDateTimeUtc());
    case TitleSourceRole:
      return titleSourceToString(row.title_source);
    case TitleGenerationInProgressRole:
      return row.title_generation_in_progress;
    case PinnedRole:
      return row.pinned;
    case PinnedAtRole:
      return row.pinned_at;
    default:
      return {};
  }
}

QHash<int, QByteArray> ConversationListModel::roleNames() const {
  return {
      {IdRole, QByteArrayLiteral("conversationId")},
      {TitleRole, QByteArrayLiteral("title")},
      {UpdatedAtRole, QByteArrayLiteral("updatedAt")},
      {TitleSourceRole, QByteArrayLiteral("titleSource")},
      {TitleGenerationInProgressRole, QByteArrayLiteral("titleGenerationInProgress")},
      {PinnedRole, QByteArrayLiteral("pinned")},
      {PinnedAtRole, QByteArrayLiteral("pinnedAt")},
  };
}

void ConversationListModel::setAll(const QList<ConversationSummary>& conversations) {
  QHash<QString, bool> progressById;
  for (const Row& row : rows_) {
    if (row.title_generation_in_progress) {
      progressById.insert(row.id, true);
    }
  }
  beginResetModel();
  rows_.clear();
  rows_.reserve(static_cast<std::size_t>(conversations.size()));
  for (const ConversationSummary& conversation : conversations) {
    Row row = toRow(conversation);
    row.title_generation_in_progress = progressById.value(row.id, false);
    rows_.push_back(std::move(row));
  }
  endResetModel();
}

void ConversationListModel::upsertToFront(const ConversationSummary& conversation) {
  Row newRow = toRow(conversation);
  const auto matchIt = std::ranges::find_if(rows_, [&newRow](const Row& row) { return row.id == newRow.id; });

  if (matchIt == rows_.end()) {
    // New conversations are never pinned — insert at the front of the unpinned sub-range.
    const int destinationRow = pinnedRowCount();
    beginInsertRows(QModelIndex(), destinationRow, destinationRow);
    rows_.insert(rows_.begin() + destinationRow, newRow);
    endInsertRows();
    return;
  }

  newRow.title_generation_in_progress = matchIt->title_generation_in_progress;
  // The incoming summary's pinned_at is never authoritative here (rename's SQL doesn't fetch it) —
  // preserve the existing row's pin state instead (REQ-F-011).
  newRow.pinned = matchIt->pinned;
  newRow.pinned_at = matchIt->pinned_at;

  const auto fromRow = static_cast<int>(std::distance(rows_.begin(), matchIt));

  if (newRow.pinned) {
    // Pinned rows are ordered by pinned_at, which rename never touches — update in place, never move.
    rows_[static_cast<std::size_t>(fromRow)] = newRow;
    const QModelIndex changedIndex = index(fromRow);
    emit dataChanged(changedIndex, changedIndex, {TitleRole, UpdatedAtRole, TitleSourceRole});
    return;
  }

  const int destinationRow = pinnedRowCount();
  if (fromRow == destinationRow) {
    rows_[static_cast<std::size_t>(fromRow)] = newRow;
    const QModelIndex changedIndex = index(fromRow);
    emit dataChanged(changedIndex, changedIndex, {TitleRole, UpdatedAtRole, TitleSourceRole});
    return;
  }

  beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), destinationRow);
  rows_.erase(rows_.begin() + fromRow);
  rows_.insert(rows_.begin() + destinationRow, newRow);
  endMoveRows();
}

void ConversationListModel::touchToFront(const QString& conversationId, const QDateTime& updatedAt) {
  const auto matchIt =
      std::ranges::find_if(rows_, [&conversationId](const Row& row) { return row.id == conversationId; });
  if (matchIt == rows_.end()) {
    return;
  }

  const auto fromRow = static_cast<int>(std::distance(rows_.begin(), matchIt));

  if (matchIt->pinned) {
    // Pinned rows are ordered by pinned_at, not updated_at — touching one never moves it.
    matchIt->updated_at = updatedAt;
    const QModelIndex changedIndex = index(fromRow);
    emit dataChanged(changedIndex, changedIndex, {UpdatedAtRole});
    return;
  }

  const int destinationRow = pinnedRowCount();
  if (fromRow == destinationRow) {
    rows_[static_cast<std::size_t>(fromRow)].updated_at = updatedAt;
    const QModelIndex changedIndex = index(fromRow);
    emit dataChanged(changedIndex, changedIndex, {UpdatedAtRole});
    return;
  }

  Row moved = *matchIt;
  moved.updated_at = updatedAt;
  beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), destinationRow);
  rows_.erase(rows_.begin() + fromRow);
  rows_.insert(rows_.begin() + destinationRow, moved);
  endMoveRows();
}

void ConversationListModel::removeById(const QString& conversationId) {
  const auto matchIt =
      std::ranges::find_if(rows_, [&conversationId](const Row& row) { return row.id == conversationId; });
  if (matchIt == rows_.end()) {
    return;
  }
  const auto rowIndex = static_cast<int>(std::distance(rows_.begin(), matchIt));
  beginRemoveRows(QModelIndex(), rowIndex, rowIndex);
  rows_.erase(matchIt);
  endRemoveRows();
}

void ConversationListModel::setTitleGenerationInProgress(const QString& conversationId, bool inProgress) {
  const auto matchIt =
      std::ranges::find_if(rows_, [&conversationId](const Row& row) { return row.id == conversationId; });
  if (matchIt == rows_.end() || matchIt->title_generation_in_progress == inProgress) {
    return;
  }
  matchIt->title_generation_in_progress = inProgress;
  const int row = static_cast<int>(std::distance(rows_.begin(), matchIt));
  const QModelIndex changedIndex = index(row);
  emit dataChanged(changedIndex, changedIndex, {TitleGenerationInProgressRole});
}

void ConversationListModel::markPinned(const QString& conversationId, const QDateTime& pinnedAt) {
  const auto matchIt =
      std::ranges::find_if(rows_, [&conversationId](const Row& row) { return row.id == conversationId; });
  if (matchIt == rows_.end() || matchIt->pinned) {
    return;
  }

  Row moved = *matchIt;
  moved.pinned = true;
  moved.pinned_at = pinnedAt;

  const auto fromRow = static_cast<int>(std::distance(rows_.begin(), matchIt));
  if (fromRow == 0) {
    rows_.front() = moved;
    const QModelIndex changedIndex = index(0);
    emit dataChanged(changedIndex, changedIndex, {PinnedRole, PinnedAtRole});
    return;
  }

  beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), 0);
  rows_.erase(matchIt);
  rows_.insert(rows_.begin(), moved);
  endMoveRows();
  const QModelIndex changedIndex = index(0);
  emit dataChanged(changedIndex, changedIndex, {PinnedRole, PinnedAtRole});
}

void ConversationListModel::markUnpinned(const QString& conversationId) {
  const auto matchIt =
      std::ranges::find_if(rows_, [&conversationId](const Row& row) { return row.id == conversationId; });
  if (matchIt == rows_.end() || !matchIt->pinned) {
    return;
  }

  Row moved = *matchIt;
  moved.pinned = false;
  moved.pinned_at = QDateTime();

  const auto fromRow = static_cast<int>(std::distance(rows_.begin(), matchIt));

  // Insertion target: the first unpinned row whose updated_at is strictly older than this row's —
  // matches the updated_at DESC ordering setAll()/touchToFront() already maintain for the unpinned
  // tail. If none is older, the row belongs at the very end.
  const auto destinationIt = std::ranges::find_if(
      rows_, [&moved](const Row& row) { return !row.pinned && row.updated_at < moved.updated_at; });
  const auto destinationRow = static_cast<int>(std::distance(rows_.begin(), destinationIt));

  if (destinationRow == fromRow + 1) {
    // Already immediately before its target slot — no reordering needed, only the pin fields change.
    rows_[static_cast<std::size_t>(fromRow)] = moved;
    const QModelIndex changedIndex = index(fromRow);
    emit dataChanged(changedIndex, changedIndex, {PinnedRole, PinnedAtRole});
    return;
  }

  beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), destinationRow);
  rows_.erase(rows_.begin() + fromRow);
  const auto insertPos = destinationRow > fromRow ? destinationRow - 1 : destinationRow;
  rows_.insert(rows_.begin() + insertPos, moved);
  endMoveRows();
  const QModelIndex changedIndex = index(insertPos);
  emit dataChanged(changedIndex, changedIndex, {PinnedRole, PinnedAtRole});
}

int ConversationListModel::visibleCount(bool pinned, const QString& filterText) const {
  int count = 0;
  for (const Row& row : rows_) {
    if (row.pinned != pinned) {
      continue;
    }
    if (filterText.isEmpty() || row.title.contains(filterText, Qt::CaseInsensitive)) {
      ++count;
    }
  }
  return count;
}

std::optional<QString> ConversationListModel::firstConversationId() const {
  if (rows_.empty()) {
    return std::nullopt;
  }
  return rows_.front().id;
}

bool ConversationListModel::isEmpty() const { return rows_.empty(); }

int ConversationListModel::pinnedRowCount() const {
  return static_cast<int>(std::ranges::count_if(rows_, [](const Row& row) { return row.pinned; }));
}

ConversationListModel::Row ConversationListModel::toRow(const ConversationSummary& conversation) {
  Row row{
      .id = conversation.id,
      .title = conversation.title,
      .updated_at = conversation.updated_at,
      .title_source = conversation.title_source,
  };
  row.pinned = conversation.pinned_at.has_value();
  row.pinned_at = conversation.pinned_at.value_or(QDateTime());
  return row;
}

QString ConversationListModel::formatRelativeTime(const QDateTime& updatedAt, const QDateTime& now) {
  if (!updatedAt.isValid() || !now.isValid()) {
    return {};
  }

  const qint64 elapsedSeconds = updatedAt.secsTo(now);
  if (elapsedSeconds < 60) {
    return QStringLiteral("now");
  }

  const qint64 elapsedMinutes = elapsedSeconds / 60;
  if (elapsedMinutes < 60) {
    return QStringLiteral("%1m ago").arg(elapsedMinutes);
  }

  const qint64 elapsedHours = elapsedMinutes / 60;
  if (elapsedHours < 24) {
    return QStringLiteral("%1h ago").arg(elapsedHours);
  }

  return QStringLiteral("%1d ago").arg(elapsedHours / 24);
}

}  // namespace holonight_application
