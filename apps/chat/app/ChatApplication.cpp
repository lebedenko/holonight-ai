#include "ChatApplication.h"

#include "ChatDbusAdaptor.h"
#include "holonight_application/chat_view_model.h"
#include "holonight_platform/desktop_notifier.h"
#include "holonight_platform/panel_surface.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

ChatApplication::ChatApplication(int& argc, char** argv)
    : QGuiApplication(argc, argv), engine_{std::make_unique<QQmlEngine>()} {
  setApplicationName(QStringLiteral("holonight-chat"));
  setApplicationVersion(QStringLiteral(HOLONIGHT_CHAT_VERSION));
  setQuitOnLastWindowClosed(false);

  if (QFileInfo{applicationFilePath()}.canonicalFilePath() ==
      QFileInfo{QStringLiteral(HOLONIGHT_BUILD_EXECUTABLE)}.canonicalFilePath()) {
    engine_->addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  } else {
    engine_->addImportPath(QDir{applicationDirPath()}.absoluteFilePath(QStringLiteral(HOLONIGHT_INSTALL_QML_PATH)));
  }
  engine_->rootContext()->setContextProperty(QStringLiteral("ChatApplication"), this);

  QDBusConnection bus = QDBusConnection::sessionBus();
  primary_instance_ = bus.registerService(QStringLiteral("org.holonight.Chat"));
  if (!primary_instance_) {
    QDBusInterface existing{QStringLiteral("org.holonight.Chat"), QStringLiteral("/org/holonight/Chat"),
                            QStringLiteral("org.holonight.Chat1"), bus};
    existing.asyncCall(QStringLiteral("ShowWorkspace"));
    QTimer::singleShot(0, this, &QCoreApplication::quit);
    return;
  }
  new ChatDbusAdaptor(*this);
  if (!bus.registerObject(QStringLiteral("/org/holonight/Chat"), this, QDBusConnection::ExportAdaptors)) {
    qFatal("Failed to register /org/holonight/Chat on the session bus");
  }

  createWorkspace();
  panel_surface_ = std::make_unique<holonight_platform::PanelSurface>(*engine_, this);
  QObject::connect(panel_surface_.get(), &holonight_platform::PanelSurface::opened, this,
                   [this](const QString& output_name) {
                     Q_EMIT PanelVisibilityChanged(true, output_name);
                     if (collapse_pending_) {
                       collapse_pending_ = false;
                       workspace_window_->hide();
                     }
                   });
  QObject::connect(panel_surface_.get(), &holonight_platform::PanelSurface::closed, this, [this]() {
    collapse_pending_ = false;
    Q_EMIT PanelVisibilityChanged(false, QString{});
    if (restore_workspace_on_panel_close_) {
      ShowWorkspace();
    }
    restore_workspace_on_panel_close_ = true;
  });
  QObject::connect(panel_surface_.get(), &holonight_platform::PanelSurface::failed, this,
                   [this](const QString&) { collapse_pending_ = false; });
  QObject::connect(workspace_window_.get(), &QQuickWindow::visibleChanged, this,
                   [this] { Q_EMIT WorkspaceVisibilityChanged(workspaceVisible()); });
  QObject::connect(workspace_window_.get(), &QQuickWindow::closing, this, [this](QQuickCloseEvent*) {
    if (!panel_surface_->isOpen()) {
      QTimer::singleShot(0, this, &QCoreApplication::quit);
    }
  });

  desktop_notifier_ = std::make_unique<holonight_platform::DesktopNotifier>(this);
  QObject::connect(desktop_notifier_.get(), &holonight_platform::DesktopNotifier::activated, this,
                   &ChatApplication::ShowWorkspace);
  if (auto* view_model =
          engine_->singletonInstance<holonight_application::ChatViewModel*>("HolonightChat", "ChatViewModel")) {
    QObject::connect(view_model, &holonight_application::ChatViewModel::responseReady, this,
                     [this](const QString& title) {
                       if (workspaceVisible() || panelVisible()) {
                         return;
                       }
                       desktop_notifier_->notify(holonight_platform::resolveNotificationSummary(title),
                                                 QStringLiteral("Response ready"));
                     });
    QObject::connect(view_model, &holonight_application::ChatViewModel::requestFailed, this,
                     [this](const QString& title, const QString& message) {
                       if (workspaceVisible() || panelVisible()) {
                         return;
                       }
                       desktop_notifier_->notify(holonight_platform::resolveNotificationSummary(title), message);
                     });
  }

  QObject::connect(this, &QGuiApplication::aboutToQuit, this, [engine = engine_.get()]() {
    if (auto* view_model =
            engine->singletonInstance<holonight_application::ChatViewModel*>("HolonightChat", "ChatViewModel")) {
      view_model->stop();
    }
  });

  ShowWorkspace();
}

ChatApplication::~ChatApplication() = default;

bool ChatApplication::isPrimaryInstance() const { return primary_instance_; }

bool ChatApplication::workspaceVisible() const {
  return workspace_window_ != nullptr && workspace_window_->isVisible();
}

bool ChatApplication::panelVisible() const { return panel_surface_ != nullptr && panel_surface_->isConfigured(); }

void ChatApplication::ShowWorkspace() {  // NOLINT(readability-identifier-naming) - D-Bus slot name
  collapse_pending_ = false;
  if (panel_surface_ != nullptr && panel_surface_->isOpen()) {
    restore_workspace_on_panel_close_ = false;
    panel_surface_->close();
  }
  if (workspace_window_ == nullptr) {
    return;
  }
  workspace_window_->show();
  workspace_window_->raise();
  workspace_window_->requestActivate();
}

void ChatApplication::ShowPanel(
    const QString& output_name) {  // NOLINT(readability-identifier-naming) - D-Bus slot name
  collapse_pending_ = false;
  restore_workspace_on_panel_close_ = true;
  if (panel_surface_ != nullptr) {
    static_cast<void>(panel_surface_->open(resolveOutputName(output_name)));
  }
}

void ChatApplication::CollapseToPanel(
    const QString& output_name) {  // NOLINT(readability-identifier-naming) - D-Bus slot name
  if (panel_surface_ == nullptr) {
    return;
  }
  restore_workspace_on_panel_close_ = true;
  if (panel_surface_->isConfigured()) {
    workspace_window_->hide();
    return;
  }
  collapse_pending_ = true;
  if (!panel_surface_->open(resolveOutputName(output_name))) {
    collapse_pending_ = false;
  }
}

void ChatApplication::ClosePanel(bool show_workspace) {
  collapse_pending_ = false;
  restore_workspace_on_panel_close_ = show_workspace;
  if (panel_surface_ != nullptr && panel_surface_->isOpen()) {
    panel_surface_->close();
  } else if (show_workspace) {
    ShowWorkspace();
  }
}

void ChatApplication::TogglePanel(
    const QString& output_name) {  // NOLINT(readability-identifier-naming) - D-Bus slot name
  if (panel_surface_ != nullptr && panel_surface_->isOpen()) {
    ClosePanel(true);
    return;
  }
  CollapseToPanel(output_name);
}

QString ChatApplication::resolveOutputName(const QString& output_name)
    const {  // NOLINT(readability-convert-member-functions-to-static) - accesses member workspace_window_
  if (!output_name.isEmpty() || workspace_window_ == nullptr || workspace_window_->screen() == nullptr) {
    return output_name;
  }
  return workspace_window_->screen()->name();
}

void ChatApplication::createWorkspace() {  // NOLINT(readability-convert-member-functions-to-static) - accesses member
                                           // engine_
  QQmlComponent component{engine_.get(), QUrl{QStringLiteral("qrc:/HolonightChat/workspace/WorkspaceWindow.qml")}};
  std::unique_ptr<QObject> object{component.create()};
  if (object == nullptr) {
    const QByteArray errors = component.errorString().toUtf8();
    qFatal("Failed to create WorkspaceWindow.qml: %s", errors.constData());
  }
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (window == nullptr) {
    qFatal("WorkspaceWindow.qml root object must derive from Window");
  }
  static_cast<void>(object.release());
  workspace_window_.reset(window);
}
