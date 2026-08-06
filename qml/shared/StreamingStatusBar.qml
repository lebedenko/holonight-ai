import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

HnSurfaceFrame {
    id: root

    visible: ChatViewModel.isStreaming
    implicitHeight: visible ? content.implicitHeight + HoloniightPalette.controlPadding * 2 : 0
    surfaceRole: HnSurfaceRole.Control
    fillColor: HoloniightPalette.surface
    borderColor: HoloniightPalette.borderPassive
    borderWidth: HoloniightPalette.borderWidth

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.margins: HoloniightPalette.controlPadding
        spacing: HoloniightPalette.controlPadding

        HnStatusIndicator {
            Layout.fillWidth: true
            status: HnStatusIndicator.Info
            text: qsTr("Generating response…")
        }

        Button {
            text: qsTr("Stop")
            onClicked: ChatViewModel.stop()
        }
    }
}
