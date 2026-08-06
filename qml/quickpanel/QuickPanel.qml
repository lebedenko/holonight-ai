pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property string outputName

    Keys.onEscapePressed: {
        if (!quickPanelHeader.dropdownOpen)
            ChatApplication.ClosePanel(true);
    }

    HnSurfaceFrame {
        anchors.fill: parent
        surfaceRole: HnSurfaceRole.Panel

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            QuickPanelHeader {
                id: quickPanelHeader

                Layout.fillWidth: true
                onNewChatRequested: quickChatPanel.focusComposer()
            }

            ChatHeader {
                Layout.fillWidth: true
                compact: true
            }

            ChatPanel {
                id: quickChatPanel

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: HoloniightPalette.controlPadding * 2
                compact: true
                showHeader: false
            }
        }
    }

    Component.onCompleted: forceActiveFocus()
}
