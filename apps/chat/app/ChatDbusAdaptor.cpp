#include "ChatDbusAdaptor.h"

#include "ChatApplication.h"

ChatDbusAdaptor::ChatDbusAdaptor(ChatApplication& application)
    : QDBusAbstractAdaptor(&application), application_(application) {
  connect(&application_, &ChatApplication::WorkspaceVisibilityChanged, this,
          &ChatDbusAdaptor::WorkspaceVisibilityChanged);
  connect(&application_, &ChatApplication::PanelVisibilityChanged, this, &ChatDbusAdaptor::PanelVisibilityChanged);
}

void ChatDbusAdaptor::ShowWorkspace() { application_.ShowWorkspace(); }

void ChatDbusAdaptor::ShowPanel(const QString& output_name) { application_.ShowPanel(output_name); }

void ChatDbusAdaptor::CollapseToPanel(const QString& output_name) { application_.CollapseToPanel(output_name); }

void ChatDbusAdaptor::ClosePanel(bool show_workspace) { application_.ClosePanel(show_workspace); }

void ChatDbusAdaptor::TogglePanel(const QString& output_name) { application_.TogglePanel(output_name); }
