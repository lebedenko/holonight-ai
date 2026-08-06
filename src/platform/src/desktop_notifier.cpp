#include "holonight_platform/desktop_notifier.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStringList>
#include <QVariantMap>

namespace holonight_platform {

QString resolveNotificationSummary(const QString& title) {
  return title.isEmpty() ? QStringLiteral("New response") : title;
}

DesktopNotifier::DesktopNotifier(QObject* parent)
    : QObject(parent),
      interface_{QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                 QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus()} {
  QDBusConnection::sessionBus().connect(
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"), this,
      SLOT(handleActionInvoked(uint, QString)));
  QDBusConnection::sessionBus().connect(
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("NotificationClosed"), this,
      SLOT(handleNotificationClosed(uint, uint)));
}

void DesktopNotifier::notify(const QString& summary, const QString& body) {
  const QStringList actions{QStringLiteral("default"), QString{}};
  const QVariantMap hints{};
  QDBusPendingCall pending_call = interface_.asyncCall(QStringLiteral("Notify"), QCoreApplication::applicationName(),
                                                       0U, QString{}, summary, body, actions, hints, -1);
  auto* watcher = new QDBusPendingCallWatcher(pending_call, this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, &DesktopNotifier::handleNotifyFinished);
}

void DesktopNotifier::handleNotifyFinished(QDBusPendingCallWatcher* watcher) {
  watcher->deleteLater();
  const QDBusPendingReply<uint> reply = *watcher;
  if (reply.isError()) {
    return;
  }
  live_notification_ids_.insert(reply.value());
}

void DesktopNotifier::handleActionInvoked(uint notification_id, const QString& actionKey) {
  if (actionKey == QStringLiteral("default") && live_notification_ids_.remove(notification_id)) {
    Q_EMIT activated();
  }
}

void DesktopNotifier::handleNotificationClosed(uint notification_id, uint reason) {
  Q_UNUSED(reason)
  live_notification_ids_.remove(notification_id);
}

}  // namespace holonight_platform
