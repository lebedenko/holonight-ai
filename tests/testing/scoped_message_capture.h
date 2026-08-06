#pragma once

#include <QString>
#include <QStringList>
#include <QtLogging>

#include <algorithm>

namespace holonight_testing {

// RAII qWarning()/qDebug() capture, installed via qInstallMessageHandler(). Several gray-box tests
// have no other seam to observe private branching logic (e.g. UtilityTaskRunner has no injectable
// providers), so the diagnostic text each branch logs is the only externally observable signal.
class ScopedMessageCapture {
 public:
  ScopedMessageCapture() : previous_handler_(qInstallMessageHandler(&ScopedMessageCapture::handle)) {
    active_capture_ = &messages_;
  }
  ~ScopedMessageCapture() {
    active_capture_ = nullptr;
    qInstallMessageHandler(previous_handler_);
  }
  ScopedMessageCapture(const ScopedMessageCapture&) = delete;
  ScopedMessageCapture& operator=(const ScopedMessageCapture&) = delete;
  ScopedMessageCapture(ScopedMessageCapture&&) = delete;
  ScopedMessageCapture& operator=(ScopedMessageCapture&&) = delete;

  [[nodiscard]] bool anyMessageContains(const QString& needle) const {
    return std::ranges::any_of(messages_, [&needle](const QString& message) { return message.contains(needle); });
  }

  [[nodiscard]] int countMessagesContaining(const QString& needle) const {
    return static_cast<int>(
        std::ranges::count_if(messages_, [&needle](const QString& message) { return message.contains(needle); }));
  }

 private:
  static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    Q_UNUSED(type)
    Q_UNUSED(context)
    if (active_capture_ != nullptr) {
      active_capture_->append(message);
    }
  }

  static inline QStringList* active_capture_ = nullptr;

  QtMessageHandler previous_handler_;
  QStringList messages_;
};

}  // namespace holonight_testing
