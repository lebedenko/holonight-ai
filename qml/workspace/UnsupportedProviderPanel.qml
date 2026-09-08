import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    property string providerName: ""

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    Item { Layout.fillHeight: true }

    HnEmptyState {
        Layout.alignment: Qt.AlignHCenter
        titleText: root.providerName
        descriptionText: qsTr("Coming soon: %1").arg(root.providerName)
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Controls.Button {
            text: qsTr("Reset")
            enabled: false
        }

        Item { Layout.fillWidth: true }

        Controls.Button {
            text: qsTr("Cancel")
            enabled: false
        }

        Controls.Button {
            text: qsTr("Save")
            enabled: false
        }
    }
}
