import QtQuick
import QtQuick.Controls.Basic as QQC2
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

HnSurfaceFrame {
    id: root

    visible: ChatViewModel.isStreaming
    implicitHeight: visible ? content.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2 : 0
    surfaceRole: HnSurfaceRole.Control
    fillColor: HoloniightPalette.surface
    borderColor: HoloniightPalette.borderPassive
    borderWidth: HnMetrics.borderWidth

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

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
