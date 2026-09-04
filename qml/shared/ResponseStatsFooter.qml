import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

RowLayout {
    id: root

    required property bool isAssistant
    required property string messageStatus
    required property var inputTokenCount
    required property var outputTokenCount
    required property var reasoningTokenCount
    required property var cacheCreationTokenCount
    required property var cacheReadTokenCount
    required property var totalTokenCount
    required property var durationMs

    objectName: "responseStatsFooter"
    // REQ-F-004/REQ-C-001/REQ-C-002/REQ-C-007: durationMs is stamped onto every
    // Usage by ChatController::stampDuration() before persistUsage() runs (see
    // chat_controller.cpp), so it is the one role guaranteed non-undefined whenever
    // a usage row exists for this message -- the most reliable "usage present" check
    // available from the flattened QML roles (there is no single "usage" role/object).
    visible: root.isAssistant && root.messageStatus === "complete" && root.durationMs !== undefined
    spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 2

    ResponseStatsPopup {
        id: responseStatsPopup

        anchorItem: statsInfoButton
        inputTokenCount: root.inputTokenCount
        outputTokenCount: root.outputTokenCount
        reasoningTokenCount: root.reasoningTokenCount
        cacheCreationTokenCount: root.cacheCreationTokenCount
        cacheReadTokenCount: root.cacheReadTokenCount
        totalTokenCount: root.totalTokenCount
        durationMs: root.durationMs
    }

    Item {
        Layout.fillWidth: true
    }

    Text {
        objectName: "promptTokenBadge"
        visible: root.inputTokenCount !== undefined
        text: qsTr("T %1").arg(TokenFormatter.formatTokenCount(root.inputTokenCount))
        textFormat: Text.PlainText
        color: HoloniightPalette.textMuted
        font.pointSize: HolonightTheme.captionSize
    }

    Text {
        objectName: "completionTokenBadge"
        visible: root.outputTokenCount !== undefined
        text: qsTr("C %1").arg(TokenFormatter.formatTokenCount(root.outputTokenCount))
        textFormat: Text.PlainText
        color: HoloniightPalette.textMuted
        font.pointSize: HolonightTheme.captionSize
    }

    Text {
        objectName: "reasoningTokenBadge"
        visible: root.reasoningTokenCount !== undefined
        text: qsTr("R %1").arg(TokenFormatter.formatTokenCount(root.reasoningTokenCount))
        textFormat: Text.PlainText
        color: HoloniightPalette.textMuted
        font.pointSize: HolonightTheme.captionSize
    }

    Text {
        objectName: "durationBadge"
        visible: root.durationMs !== undefined
        text: root.durationMs === undefined ? ""
              : (root.durationMs >= 1000 ? qsTr("%1s").arg((root.durationMs / 1000).toFixed(1))
                                          : qsTr("%1ms").arg(root.durationMs))
        textFormat: Text.PlainText
        color: HoloniightPalette.textMuted
        font.pointSize: HolonightTheme.captionSize
    }

    HnIconButton {
        id: statsInfoButton

        objectName: "responseStatsInfoButton"
        sizeRole: HnControlSize.Compact
        icon.source: "qrc:/HolonightChat/assets/icons/info.svg"
        Accessible.name: qsTr("View response stats")
        onClicked: responseStatsPopup.visible ? responseStatsPopup.close() : responseStatsPopup.open()
    }
}
