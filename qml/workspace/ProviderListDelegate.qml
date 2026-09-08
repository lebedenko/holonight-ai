pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnListDelegate {
    id: root

    required property string providerName
    required property string providerId
    required property string providerType
    required property bool providerEnabled
    property bool isSelected: false

    Layout.fillWidth: true
    title: root.providerName
    subtitle: root.providerEnabled ? qsTr("Enabled") : qsTr("Disabled")
    subtitlePresentation: HnListDelegate.SingleLine
    checked: root.isSelected
    selectionStyle: HnSelectableDelegate.Outline
    leadingContentAlignment: Qt.AlignVCenter
    leadingContent: Component {
        ProviderIcon {
            providerType: root.providerType
            framed: false
            implicitWidth: 36
            implicitHeight: 36
        }
    }
    trailingContent: Component {
        HnIcon {
            source: "qrc:/HolonightChat/assets/icons/chevron-right.svg"
            size: 16
            iconState: root.isSelected ? HnIcon.Active : HnIcon.Muted
        }
    }
}
