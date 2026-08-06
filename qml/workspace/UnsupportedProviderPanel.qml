import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import Holonight

ColumnLayout {
    id: root

    property string providerName: ""

    spacing: HoloniightPalette.controlPadding

    Item { Layout.fillHeight: true }

    HnEmptyState {
        Layout.alignment: Qt.AlignHCenter
        titleText: root.providerName
        descriptionText: qsTr("Coming soon: %1").arg(root.providerName)
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: HoloniightPalette.controlPadding

        Button {
            text: qsTr("Reset")
            enabled: false
        }

        Item { Layout.fillWidth: true }

        Button {
            text: qsTr("Cancel")
            enabled: false
        }

        Button {
            text: qsTr("Save")
            enabled: false
        }
    }
}
