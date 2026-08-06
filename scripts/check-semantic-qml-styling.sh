#!/usr/bin/env bash
set -euo pipefail

project_root="${1:?usage: check-semantic-qml-styling.sh <project-root>}"

require_text() {
  local file="$1"
  local text="$2"
  if ! grep -Fq "$text" "$project_root/$file"; then
    echo "$file must contain: $text" >&2
    exit 1
  fi
}

reject_text() {
  local file="$1"
  local text="$2"
  if grep -Fq "$text" "$project_root/$file"; then
    echo "$file must not contain: $text" >&2
    exit 1
  fi
}

reject_pattern() {
  local file="$1"
  local pattern="$2"
  if grep -Pzq "$pattern" "$project_root/$file"; then
    echo "$file contains a forbidden semantic styling override" >&2
    exit 1
  fi
}

for file in \
  qml/quickpanel/QuickPanel.qml \
  qml/quickpanel/QuickPanelHeader.qml \
  qml/workspace/ConversationListPanel.qml \
  qml/workspace/WorkspaceWindow.qml; do
  reject_pattern "$file" 'surfaceRole: HnSurfaceRole\.Panel\s+fillColor: HoloniightPalette\.surface(?:Elevated)?'
done

require_text qml/quickpanel/QuickPanelHeader.qml "surfaceRole: HnSurfaceRole.Menu"
reject_text qml/quickpanel/QuickPanelHeader.qml "fillColor: HoloniightPalette.surface"
require_text qml/workspace/WorkspaceWindow.qml "fillColor: HoloniightPalette.surface"

for file in qml/shared/UserMessageCard.qml qml/shared/MessageBubble.qml; do
  require_text "$file" "surfaceRole: HnSurfaceRole.Card"
  reject_text "$file" "HoloniightPalette.borderSubtle"
done
require_text qml/shared/UserMessageCard.qml "fillColor: HoloniightPalette.surfaceElevated"
require_text qml/shared/MessageBubble.qml "fillColor: HoloniightPalette.surface"
require_text qml/shared/MessageBubble.qml "borderWidth: root.isError ? HoloniightPalette.borderWidth : 0"

for file in qml/quickpanel/QuickPanelHeader.qml qml/workspace/UnsupportedProviderPanel.qml; do
  reject_text "$file" "font.pixelSize: 18"
done
require_text qml/quickpanel/QuickPanelHeader.qml "HnHeaderBar"
require_text qml/quickpanel/QuickPanelHeader.qml "HnAppTitle"
require_text qml/workspace/UnsupportedProviderPanel.qml "HnEmptyState"
