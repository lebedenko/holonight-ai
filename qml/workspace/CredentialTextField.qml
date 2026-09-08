pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts

import Holonight.Core
import Holonight.Controls

Controls.TextField {
    id: root
    objectName: "credentialTextField"

    property bool actionsEnabled: true
    signal clearRequested

    echoMode: revealButton.checked ? TextInput.Normal : TextInput.Password
    rightPadding: trailingActions.width + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2

    RowLayout {
        id: trailingActions

        anchors.right: parent.right
        anchors.rightMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        HnIconButton {
            id: revealButton
            objectName: "revealCredentialButton"

            sizeRole: HnControlSize.Compact
            checkable: true
            enabled: root.actionsEnabled
            icon.source: "qrc:/HolonightChat/assets/icons/reveal.svg"
            Accessible.name: checked ? qsTr("Hide credential") : qsTr("Reveal credential")
        }

        HnIconButton {
            objectName: "clearCredentialButton"
            sizeRole: HnControlSize.Compact
            enabled: root.actionsEnabled
            icon.source: "qrc:/HolonightChat/assets/icons/clear.svg"
            Accessible.name: qsTr("Clear credential")
            onClicked: root.clearRequested()
        }
    }
}
