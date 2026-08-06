#pragma once

#include <QDBusInterface>
#include <QObject>
#include <QSet>
#include <QString>

class QDBusPendingCallWatcher;

namespace holonight_platform {

// Fallback summary used when a conversation's title is empty (REQ-F-002/REQ-F-005).
[[nodiscard]] QString resolveNotificationSummary(const QString& title);

// Thin client of the standard org.freedesktop.Notifications D-Bus interface. Unrelated to
// org.holonight.Chat1 (the app's own service consumed by holonight-shell) — this class only ever
// talks to the session's notification daemon.
class DesktopNotifier : public QObject {
  Q_OBJECT

 public:
  explicit DesktopNotifier(QObject* parent = nullptr);
  ~DesktopNotifier() override = default;

  DesktopNotifier(const DesktopNotifier&) = delete;
  DesktopNotifier& operator=(const DesktopNotifier&) = delete;
  DesktopNotifier(DesktopNotifier&&) = delete;
  DesktopNotifier& operator=(DesktopNotifier&&) = delete;

  // Fire-and-forget: issues one Notify() call. Never throws, never blocks, never retries
  // (REQ-NF-002/REQ-NF-003). Safe to call even if no daemon is present on the session bus.
  void notify(const QString& summary, const QString& body);

 Q_SIGNALS:
  // Emitted when the user clicks one of this instance's live notifications. Connect this directly
  // to an in-process slot (REQ-C-001) — never route it back out over D-Bus.
  void activated();

 private Q_SLOTS:
  void handleNotifyFinished(QDBusPendingCallWatcher* watcher);
  void handleActionInvoked(uint notification_id, const QString& actionKey);
  void handleNotificationClosed(uint notification_id, uint reason);

 private:
  QDBusInterface interface_;
  QSet<uint> live_notification_ids_;
};

}  // namespace holonight_platform
