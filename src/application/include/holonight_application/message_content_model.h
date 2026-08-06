#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <holonight_rendering/content_block.h>
#include <vector>

namespace holonight_application {

class MessageContentModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by an assistant message")
  Q_PROPERTY(QString rawMarkdown READ rawMarkdown NOTIFY rawMarkdownChanged)

 public:
  enum Roles : std::uint16_t {
    BlockIdRole = Qt::UserRole + 1,
    TypeRole,
    TextRole,
    LanguageRole,
    CompleteRole,
  };
  Q_ENUM(Roles)

  explicit MessageContentModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
  [[nodiscard]] const QString& rawMarkdown() const noexcept { return source_; }

  void setSource(QString source, bool terminal);

 signals:
  void rawMarkdownChanged();
  void published();

 private:
  void publish(bool terminal);
  void reconcile(std::vector<holonight_rendering::ContentBlock> blocks);

  static constexpr int kPublishIntervalMs = 50;
  std::vector<holonight_rendering::ContentBlock> blocks_;
  QString source_;
  bool terminal_ = false;
  QTimer publish_timer_;
};

}  // namespace holonight_application
