import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property string messageRole
    required property string messageText
    required property string messageStatus
    required property string modelName
    required property string providerId
    required property string providerType
    required property string providerName
    required property date createdAt
    required property var contentBlocks
    required property var inputTokenCount
    required property var outputTokenCount
    required property var reasoningTokenCount
    required property var cacheCreationTokenCount
    required property var cacheReadTokenCount
    required property var totalTokenCount
    required property var durationMs
    property real maximumWidthRatio: 0.75

    readonly property bool isAssistant: messageRole === "assistant"
    readonly property bool isError: messageStatus === "error"
    readonly property bool isWaitingForFirstToken: isAssistant
                                                   && (messageStatus === "pending" || messageStatus === "streaming")
                                                   && messageText.length === 0
    // Tool orchestration can leave an empty assistant placeholder as either Complete or Cancelled.
    // Conversation has no message-removal API, so keep the row in the model but do not render it.
    readonly property bool isEmptyTerminalTurn: isAssistant && messageText.trim().length === 0
                                                && (messageStatus === "complete" || messageStatus === "cancelled")
    readonly property real iconSize: 64
    readonly property real iconSpacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 4
    visible: !isWaitingForFirstToken && !isEmptyTerminalTurn
    implicitHeight: (isWaitingForFirstToken || isEmptyTerminalTurn) ? 0
                     : Math.max(messageFrame.height, root.isAssistant ? root.iconSize : 0)

    ProviderIcon {
        id: icon
        objectName: "providerIcon"

        anchors.top: parent.top
        anchors.left: parent.left
        visible: root.isAssistant
        width: root.iconSize
        height: root.iconSize
        providerType: root.providerType
    }

    HnSurfaceFrame {
        id: messageFrame

        anchors.top: parent.top
        anchors.left: root.isAssistant ? icon.right : parent.left
        anchors.leftMargin: root.isAssistant ? root.iconSpacing : 0
        width: Math.max(0, root.width * Math.min(1, Math.max(0, root.maximumWidthRatio))
                           - (root.isAssistant ? root.iconSize + root.iconSpacing : 0))
        surfaceRole: HnSurfaceRole.Card
        chamferedCornersOverride: HnCornerMask.TopRight
        fillColor: HoloniightPalette.surface
        borderColor: HoloniightPalette.borderUrgent
        borderWidth: root.isError ? HnMetrics.borderWidth : 0
        height: content.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 4

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            RowLayout {
                Layout.fillWidth: true
                visible: root.isAssistant
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 2

                Text {
                    objectName: "providerAttribution"
                    text: root.providerName.length > 0
                          ? (root.modelName.length > 0 ? root.providerName + " · " + root.modelName : root.providerName)
                          : root.modelName
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
                visible: !root.isAssistant
                text: root.messageText
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                color: HoloniightPalette.textPrimary
                readOnly: true
                selectByMouse: true
            }

            AssistantResponseContent {
                Layout.fillWidth: true
                visible: root.isAssistant && root.messageText.length > 0
                contentModel: root.contentBlocks
                messageStatus: root.messageStatus
            }

            ResponseStatsFooter {
                Layout.fillWidth: true
                isAssistant: root.isAssistant
                messageStatus: root.messageStatus
                inputTokenCount: root.inputTokenCount
                outputTokenCount: root.outputTokenCount
                reasoningTokenCount: root.reasoningTokenCount
                cacheCreationTokenCount: root.cacheCreationTokenCount
                cacheReadTokenCount: root.cacheReadTokenCount
                totalTokenCount: root.totalTokenCount
                durationMs: root.durationMs
            }
        }
    }
}
