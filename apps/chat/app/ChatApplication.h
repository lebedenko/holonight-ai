#pragma once

#include <QGuiApplication>
#include <QString>

#include <memory>

namespace holonight_platform {
class DesktopNotifier;
class PanelSurface;
}  // namespace holonight_platform

class QQmlEngine;
class QQuickWindow;

class ChatApplication : public QGuiApplication {
  Q_OBJECT

 public:
  ChatApplication(int& argc, char** argv);
  ~ChatApplication() override;

  ChatApplication(const ChatApplication&) = delete;
  ChatApplication& operator=(const ChatApplication&) = delete;
  ChatApplication(ChatApplication&&) = delete;
  ChatApplication& operator=(ChatApplication&&) = delete;

  [[nodiscard]] bool isPrimaryInstance() const;
  [[nodiscard]] bool workspaceVisible() const;
  [[nodiscard]] bool panelVisible() const;

 public Q_SLOTS:
  void ShowWorkspace();                              // NOLINT(readability-identifier-naming) - D-Bus slot name
  void ShowPanel(const QString& output_name);        // NOLINT(readability-identifier-naming) - D-Bus slot name
  void CollapseToPanel(const QString& output_name);  // NOLINT(readability-identifier-naming) - D-Bus slot name
  void ClosePanel(bool show_workspace);              // NOLINT(readability-identifier-naming) - D-Bus slot name
  void TogglePanel(const QString& output_name);      // NOLINT(readability-identifier-naming) - D-Bus slot name

 Q_SIGNALS:
  void WorkspaceVisibilityChanged(bool visible);  // NOLINT(readability-identifier-naming) - D-Bus signal name
  void PanelVisibilityChanged(                    // NOLINT(readability-identifier-naming) - D-Bus signal name
      bool visible, const QString& output_name);

 private:
  void createWorkspace();
  [[nodiscard]] QString resolveOutputName(const QString& output_name) const;

  std::unique_ptr<QQmlEngine> engine_;
  std::unique_ptr<QQuickWindow> workspace_window_;
  std::unique_ptr<holonight_platform::PanelSurface> panel_surface_;
  std::unique_ptr<holonight_platform::DesktopNotifier> desktop_notifier_;
  bool primary_instance_{false};
  bool collapse_pending_{false};
  bool restore_workspace_on_panel_close_{true};
};
