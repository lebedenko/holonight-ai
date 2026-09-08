import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnApplicationWindow {
    id: root

    width: 1000
    height: 640
    minimumWidth: 1000
    minimumHeight: 640
    visible: false
    title: qsTr("HoloNight AI")
    contentPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)

    Loader {
        id: settingsLoader

        active: false
        sourceComponent: SettingsWindow {}
    }

    function openProviderSettings(providerId: string): void {
        settingsLoader.active = true
        let settingsWindow = settingsLoader.item as SettingsWindow
        settingsWindow.openProvider(providerId)
    }

    function toggleSettings(): void {
        settingsLoader.active = true
        let settingsWindow = settingsLoader.item as SettingsWindow
        settingsWindow.visible = !settingsWindow.visible
        if (settingsWindow.visible) {
            settingsWindow.raise()
            settingsWindow.requestActivate()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        ConversationListPanel {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            chamferedCornersOverride: HnCornerMask.TopRight | HnCornerMask.BottomRight
            borderColor: HoloniightPalette.borderPassive
            borderWidth: HnMetrics.borderWidth
            onSettingsToggleRequested: root.toggleSettings()
            onNewChatRequested: workspaceChatPanel.focusComposer()
        }

        HnSurfaceFrame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            surfaceRole: HnSurfaceRole.Window
            fillColor: HoloniightPalette.surface
            borderColor: HoloniightPalette.borderPassive
            borderWidth: HnMetrics.borderWidth

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ChatHeader {
                    Layout.fillWidth: true
                    onProviderSettingsRequested: providerId => root.openProviderSettings(providerId)
                    onCollapseRequested: ChatApplication.CollapseToPanel("")
                }

                ChatPanel {
                    id: workspaceChatPanel

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
                    showHeader: false
                }
            }
        }

        WorkspaceInspectorPanel {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            chamferedCornersOverride: HnCornerMask.TopLeft | HnCornerMask.BottomLeft
            borderColor: HoloniightPalette.borderPassive
            borderWidth: HnMetrics.borderWidth
        }
    }
}
