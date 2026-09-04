pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnSurfaceFrame {
    id: root

    property string currentSection: "providers"

    signal sectionSelected(string sectionId)

    readonly property var sections: [
        { "id": "general", "name": qsTr("General"), "icon": "general", "enabled": false },
        { "id": "providers", "name": qsTr("Providers"), "icon": "providers", "enabled": true },
        { "id": "background-ai", "name": qsTr("Background AI"), "icon": "background-ai", "enabled": true },
        { "id": "models", "name": qsTr("Models"), "icon": "models", "enabled": false },
        { "id": "tools", "name": qsTr("Tools & MCP"), "icon": "tools", "enabled": false },
        { "id": "permissions", "name": qsTr("Permissions"), "icon": "permissions", "enabled": false },
        { "id": "storage", "name": qsTr("Storage"), "icon": "storage", "enabled": false },
        { "id": "appearance", "name": qsTr("Appearance"), "icon": "appearance", "enabled": false },
        { "id": "advanced", "name": qsTr("Advanced"), "icon": "advanced", "enabled": false },
    ]

    surfaceRole: HnSurfaceRole.Panel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        HnAppTitle {
            Layout.fillWidth: true
            Layout.bottomMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
            applicationName: qsTr("Settings")
            skipBranding: true
            iconSource: "qrc:/HolonightChat/assets/holonight-ai.svg"
        }

        Repeater {
            model: root.sections

            delegate: HnNavigationDelegate {
                id: entryDelegate

                required property var modelData
                required property int index

                Layout.fillWidth: true
                enabled: entryDelegate.modelData.enabled
                title: entryDelegate.modelData.name
                badgeText: entryDelegate.modelData.enabled ? "" : qsTr("Soon")
                checked: entryDelegate.modelData.enabled && entryDelegate.modelData.id === root.currentSection
                Accessible.description: entryDelegate.modelData.enabled
                    ? qsTr("Current settings section")
                    : qsTr("%1 settings are not yet available").arg(entryDelegate.modelData.name)
                onClicked: if (entryDelegate.modelData.enabled) root.sectionSelected(entryDelegate.modelData.id)
                leadingContent: Component {
                    HnIcon {
                        source: "qrc:/HolonightChat/assets/icons/settings-" + entryDelegate.modelData.icon + ".svg"
                        size: 20
                        iconState: entryDelegate.checked ? HnIcon.Active
                            : (entryDelegate.enabled ? HnIcon.Normal : HnIcon.Disabled)
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
