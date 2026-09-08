import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    visible: ChatViewModel.persistenceStatusMessage.length > 0
        || ChatViewModel.providerStatusMessage.length > 0
        || ChatViewModel.errorMessage.length > 0
    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.persistenceStatusMessage.length > 0
        implicitHeight: persistenceText.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
        surfaceRole: HnSurfaceRole.Hud
        fillColor: HoloniightPalette.surfaceElevated

        RowLayout {
            anchors.fill: parent
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            Text {
                id: persistenceText

                Layout.fillWidth: true
                text: ChatViewModel.persistenceStatusMessage
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: HoloniightPalette.textPrimary
            }

            Controls.Button {
                text: qsTr("Dismiss")
                onClicked: ChatViewModel.dismissPersistenceBanner()
            }
        }
    }

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.providerStatusMessage.length > 0
        implicitHeight: providerStatusText.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
        surfaceRole: HnSurfaceRole.Hud
        fillColor: HoloniightPalette.surfaceElevated

        Text {
            id: providerStatusText

            anchors.fill: parent
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            text: ChatViewModel.providerStatusMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: HoloniightPalette.textPrimary
        }
    }

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.errorMessage.length > 0
        implicitHeight: errorText.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
        surfaceRole: HnSurfaceRole.Control
        cornerStyleOverride: HnCornerStyle.Rounded
        fillColor: HoloniightPalette.error
        borderColor: HoloniightPalette.borderUrgent

        Text {
            id: errorText

            anchors.fill: parent
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
            text: ChatViewModel.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: HoloniightPalette.onError
        }
    }
}
