import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property string messageText
    required property date createdAt
    property real maximumWidthRatio: 0.75

    implicitHeight: messageFrame.height

    HnSurfaceFrame {
        id: messageFrame

        width: Math.max(0, root.width * Math.min(1, Math.max(0, root.maximumWidthRatio)))
        anchors.right: parent.right
        surfaceRole: HnSurfaceRole.Card
        chamferedCornersOverride: HnCornerMask.TopRight
        fillColor: HoloniightPalette.surfaceElevated
        borderWidth: 0
        height: content.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 4

        RowLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: HnMetrics.controlHeight(HnControlSize.Normal) * 0.75
                Layout.preferredHeight: Layout.preferredWidth
                radius: width / 2
                color: HoloniightPalette.accentViolet

                Text {
                    anchors.centerIn: parent
                    text: "A"
                    textFormat: Text.PlainText
                    color: HoloniightPalette.onPrimary
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 2

                    Text {
                        text: qsTr("You")
                        textFormat: Text.PlainText
                        color: HoloniightPalette.textPrimary
                        font.bold: true
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        text: Qt.formatTime(root.createdAt, "HH:mm")
                        textFormat: Text.PlainText
                        color: HoloniightPalette.textMuted
                    }
                }

                TextEdit {
                    Layout.fillWidth: true
                    text: root.messageText
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    color: HoloniightPalette.textPrimary
                    readOnly: true
                    selectByMouse: true
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
        }
    }
}
