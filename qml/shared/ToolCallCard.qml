import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

// REQ-F-007/REQ-F-011: renders a tool-call Invocation or Result message entry as its own,
// visually distinct card -- never folded into MessageBubble. Deliberately generic over tool
// identity (no branching on toolName): both kinds are rendered from their structured data alone.
Item {
    id: root

    required property var toolCall
    required property date createdAt
    property real maximumWidthRatio: 0.75

    readonly property bool isInvocation: root.toolCall.kind === "invocation"
    readonly property bool isError: root.toolCall.kind === "result" && root.toolCall.isError === true
    readonly property string headerText: root.isInvocation ? qsTr("Tool: %1").arg(root.toolCall.toolName)
                                          : (root.isError ? qsTr("Tool result — error") : qsTr("Tool result"))
    readonly property string bodyText: JSON.stringify(root.isInvocation ? root.toolCall.input : root.toolCall.result,
                                                       null, 2)

    implicitHeight: messageFrame.height

    HnSurfaceFrame {
        id: messageFrame
        objectName: "toolCallCardFrame"

        anchors.top: parent.top
        anchors.left: parent.left
        width: Math.max(0, root.width * Math.min(1, Math.max(0, root.maximumWidthRatio)))
        surfaceRole: HnSurfaceRole.Card
        chamferedCornersOverride: HnCornerMask.TopRight
        fillColor: HoloniightPalette.surface
        borderColor: root.isError ? HoloniightPalette.borderUrgent : HoloniightPalette.borderSubtle
        borderWidth: HoloniightPalette.borderWidth
        height: content.implicitHeight + HoloniightPalette.controlPadding * 4

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: HoloniightPalette.controlPadding * 2
            spacing: HoloniightPalette.controlPadding

            RowLayout {
                Layout.fillWidth: true
                spacing: HoloniightPalette.controlPadding / 2

                Rectangle {
                    Layout.preferredWidth: HoloniightPalette.controlHeight * 0.6
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: root.isError ? HoloniightPalette.error : HoloniightPalette.accentCyan

                    Text {
                        anchors.centerIn: parent
                        text: "T"
                        textFormat: Text.PlainText
                        color: HoloniightPalette.onPrimary
                        font.bold: true
                    }
                }

                Text {
                    objectName: "toolCallHeader"
                    text: root.headerText
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
                objectName: "toolCallBody"
                Layout.fillWidth: true
                text: root.bodyText
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                font.family: HolonightTheme.fixedFont
                color: HoloniightPalette.textPrimary
            }
        }
    }
}
