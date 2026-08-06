#include "holonight_application/message_content_model.h"

#include <QVariant>

#include <holonight_rendering/message_content_parser.h>

namespace holonight_application {

MessageContentModel::MessageContentModel(QObject* parent) : QAbstractListModel(parent) {
  publish_timer_.setSingleShot(true);
  publish_timer_.setInterval(kPublishIntervalMs);
  connect(&publish_timer_, &QTimer::timeout, this, [this] { publish(false); });
}

int MessageContentModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(blocks_.size());
}

QVariant MessageContentModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= blocks_.size()) {
    return {};
  }
  const auto& block = blocks_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case BlockIdRole:
      return block.id();
    case TypeRole:
      return QVariant::fromValue(block.type());
    case TextRole:
      return block.text();
    case LanguageRole:
      return block.language();
    case CompleteRole:
      return block.complete();
    default:
      return {};
  }
}

QHash<int, QByteArray> MessageContentModel::roleNames() const {
  return {
      {BlockIdRole, QByteArrayLiteral("blockId")},   {TypeRole, QByteArrayLiteral("type")},
      {TextRole, QByteArrayLiteral("text")},         {LanguageRole, QByteArrayLiteral("language")},
      {CompleteRole, QByteArrayLiteral("complete")},
  };
}

void MessageContentModel::setSource(QString source, bool terminal) {
  if (source_ != source) {
    source_ = std::move(source);
    emit rawMarkdownChanged();
  }
  terminal_ = terminal;
  if (terminal) {
    publish_timer_.stop();
    publish(true);
    return;
  }
  if (!publish_timer_.isActive()) {
    publish_timer_.start();
  }
}

void MessageContentModel::publish(bool terminal) {
  if (terminal_ && !terminal) {
    return;
  }
  reconcile(holonight_rendering::MessageContentParser::parse(source_, terminal));
  emit published();
}

void MessageContentModel::reconcile(std::vector<holonight_rendering::ContentBlock> blocks) {
  std::size_t common = 0;
  while (common < blocks_.size() && common < blocks.size() && blocks_[common].id() == blocks[common].id() &&
         blocks_[common].type() == blocks[common].type()) {
    QList<int> roles;
    if (blocks_[common].text() != blocks[common].text()) {
      roles.append(TextRole);
    }
    if (blocks_[common].language() != blocks[common].language()) {
      roles.append(LanguageRole);
    }
    if (blocks_[common].complete() != blocks[common].complete()) {
      roles.append(CompleteRole);
    }
    blocks_[common] = blocks[common];
    if (!roles.isEmpty()) {
      const QModelIndex changed = index(static_cast<int>(common));
      emit dataChanged(changed, changed, roles);
    }
    ++common;
  }

  if (common < blocks_.size()) {
    beginRemoveRows({}, static_cast<int>(common), static_cast<int>(blocks_.size() - 1));
    blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(common), blocks_.end());
    endRemoveRows();
  }
  if (common < blocks.size()) {
    beginInsertRows({}, static_cast<int>(common), static_cast<int>(blocks.size() - 1));
    blocks_.insert(blocks_.end(), blocks.begin() + static_cast<std::ptrdiff_t>(common), blocks.end());
    endInsertRows();
  }
}

}  // namespace holonight_application
