import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

ColumnLayout {
    id: root

    visible: ChatViewModel.persistenceStatusMessage.length > 0
        || ChatViewModel.providerStatusMessage.length > 0
        || ChatViewModel.errorMessage.length > 0
    spacing: HoloniightPalette.controlPadding

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.persistenceStatusMessage.length > 0
        implicitHeight: persistenceText.implicitHeight + HoloniightPalette.controlPadding * 2
        surfaceRole: HnSurfaceRole.Hud
        fillColor: HoloniightPalette.surfaceElevated

        RowLayout {
            anchors.fill: parent
            anchors.margins: HoloniightPalette.controlPadding
            spacing: HoloniightPalette.controlPadding

            Text {
                id: persistenceText

                Layout.fillWidth: true
                text: ChatViewModel.persistenceStatusMessage
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: HoloniightPalette.textPrimary
            }

            Button {
                text: qsTr("Dismiss")
                onClicked: ChatViewModel.dismissPersistenceBanner()
            }
        }
    }

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.providerStatusMessage.length > 0
        implicitHeight: providerStatusText.implicitHeight + HoloniightPalette.controlPadding * 2
        surfaceRole: HnSurfaceRole.Hud
        fillColor: HoloniightPalette.surfaceElevated

        Text {
            id: providerStatusText

            anchors.fill: parent
            anchors.margins: HoloniightPalette.controlPadding
            text: ChatViewModel.providerStatusMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: HoloniightPalette.textPrimary
        }
    }

    HnSurfaceFrame {
        Layout.fillWidth: true
        visible: ChatViewModel.errorMessage.length > 0
        implicitHeight: errorText.implicitHeight + HoloniightPalette.controlPadding * 2
        surfaceRole: HnSurfaceRole.Control
        cornerStyleOverride: HnCornerStyle.Rounded
        fillColor: HoloniightPalette.error
        borderColor: HoloniightPalette.borderUrgent

        Text {
            id: errorText

            anchors.fill: parent
            anchors.margins: HoloniightPalette.controlPadding
            text: ChatViewModel.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: HoloniightPalette.onError
        }
    }
}
