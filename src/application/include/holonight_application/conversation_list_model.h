#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <holonight_domain/holonight_domain.h>
#include <holonight_persistence/conversation_record.h>
#include <optional>
#include <vector>

namespace holonight_application {

// Bridges ConversationRepository's results to a QML ListView. Follows MessageListModel's
// narrow-mutator precedent: each mutator is a direct, minimal QAbstractListModel operation tied to
// exactly one repository signal, never a generic "resync everything" path.
class ConversationListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Instantiated only by ChatViewModel; do not construct from QML")

 public:
  enum Roles : std::uint16_t {  // NOLINT(cppcoreguidelines-use-enum-class): Qt model roles are int-compatible.
    IdRole = Qt::UserRole + 1,
    TitleRole,
    UpdatedAtRole,  // pre-formatted QString for display
    TitleSourceRole,
    TitleGenerationInProgressRole,
    PinnedRole,
    PinnedAtRole,
  };
  Q_ENUM(Roles)

  explicit ConversationListModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  // Full reset — conversationListLoaded, already sorted updated_at DESC by the repository's SQL.
  void setAll(const QList<holonight_persistence::ConversationSummary>& conversations);

  // Inserts (if new) or moves-and-updates (if existing) — conversationCreated and conversationRenamed
  // both imply a fresh updated_at. Branches on the *existing* row's pinned flag, never the incoming
  // summary's (rename never carries authoritative pin data): a pinned row is updated in place and
  // never moves (REQ-F-011); an unpinned row moves to the front of the unpinned sub-range
  // (pinnedRowCount()), not absolute index 0.
  void upsertToFront(const holonight_persistence::ConversationSummary& conversation);

  // Moves an existing row to front of its section and refreshes its timestamp only —
  // lastModelIdUpdated. Pinned rows never move (ordered by pinned_at, not updated_at); unpinned rows
  // move to pinnedRowCount(), not absolute index 0.
  void touchToFront(const QString& conversationId, const QDateTime& updatedAt);

  void removeById(const QString& conversationId);
  void setTitleGenerationInProgress(const QString& conversationId, bool inProgress);

  // Moves a row into the Pinned block at index 0 (most-recently-pinned always leads). No-op if the
  // row doesn't exist or is already pinned.
  void markPinned(const QString& conversationId, const QDateTime& pinnedAt);

  // Moves a row out of the Pinned block, re-inserting it among the unpinned rows at the position
  // its updated_at implies. No-op if the row doesn't exist or is already unpinned.
  void markUnpinned(const QString& conversationId);

  // Number of rows matching `pinned`, restricted to titles containing `filterText`
  // case-insensitively (empty filterText matches everything). Drives ConversationListPanel.qml's
  // per-section header visibility.
  [[nodiscard]] Q_INVOKABLE int visibleCount(bool pinned, const QString& filterText) const;

  // Plain C++ accessors (not Q_PROPERTY/Q_INVOKABLE) — used by ChatViewModel's delete-fallback
  // logic, mirroring ChatViewModel::conversation()'s own C++-only precedent.
  [[nodiscard]] std::optional<QString> firstConversationId() const;
  [[nodiscard]] bool isEmpty() const;

 private:
  struct Row {
    QString id;
    QString title;
    QDateTime updated_at;
    holonight_domain::TitleSource title_source = holonight_domain::TitleSource::Fallback;
    bool title_generation_in_progress = false;
    bool pinned = false;
    QDateTime pinned_at;  // valid only when pinned == true
  };

  [[nodiscard]] static Row toRow(const holonight_persistence::ConversationSummary& conversation);
  [[nodiscard]] static QString formatRelativeTime(const QDateTime& updatedAt, const QDateTime& now);

  // Index of the first unpinned row — equivalently, the count of pinned rows, since the Pinned block
  // is always maintained at the front of rows_. "Front of the unpinned sub-range" for upsertToFront()
  // and touchToFront() (§6.2 of DESIGN.md).
  [[nodiscard]] int pinnedRowCount() const;

  std::vector<Row> rows_;
  QTimer relative_time_refresh_timer_;
};

}  // namespace holonight_application
