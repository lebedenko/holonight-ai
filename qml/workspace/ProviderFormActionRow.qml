pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core

RowLayout {
    id: root

    property Component fieldContent
    property Component actionContent
    property real actionColumnWidth: 220

    spacing: HoloniightPalette.controlPadding * 2

    Loader {
        Layout.fillWidth: true
        sourceComponent: root.fieldContent
    }

    Loader {
        Layout.preferredWidth: root.actionColumnWidth
        Layout.alignment: Qt.AlignBottom
        sourceComponent: root.actionContent
    }
}
