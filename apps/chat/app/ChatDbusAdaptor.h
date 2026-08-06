#pragma once

#include <QDBusAbstractAdaptor>
#include <QString>

class ChatApplication;

class ChatDbusAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.holonight.Chat1")

 public:
  explicit ChatDbusAdaptor(ChatApplication& application);

 public Q_SLOTS:
  void ShowWorkspace();
  void ShowPanel(const QString& output_name);
  void CollapseToPanel(const QString& output_name);
  void ClosePanel(bool show_workspace);
  void TogglePanel(const QString& output_name);

 Q_SIGNALS:
  void WorkspaceVisibilityChanged(bool visible);
  void PanelVisibilityChanged(bool visible, const QString& output_name);

 private:
  ChatApplication& application_;
};
