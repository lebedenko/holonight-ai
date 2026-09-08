import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Controls.Popup {
    id: root

    // The button (or other item) this popup is anchored below-right of.
    required property Item anchorItem
    required property var inputTokenCount
    required property var outputTokenCount
    required property var reasoningTokenCount
    required property var cacheCreationTokenCount
    required property var cacheReadTokenCount
    required property var totalTokenCount
    required property var durationMs

    objectName: "responseStatsPopup"
    modal: false
    focus: true
    closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
    padding: HnMetrics.horizontalPadding(HnControlSize.Normal)

    // Reparent to the window's overlay layer (not ResponseStatsFooter's own parent inside an
    // opaque HnSurfaceFrame) so this popup's stacking order is independent of
    // whatever else that parent later draws on top -- Popup does not do this automatically, only
    // its optional dimming background does. See Qt's Overlay docs: "Overlay provides a layer for
    // popups, ensuring that popups are displayed above other content."
    parent: Controls.Overlay.overlay

    // mapToItem() is not a reactive dependency on every ancestor's position. Calculate when the
    // popup opens so delegates that were laid out or scrolled after construction use their current
    // screen position.
    function positionAtAnchor() {
        const anchorBottomRight = root.anchorItem.mapToItem(root.parent, root.anchorItem.width,
                                                            root.anchorItem.height);
        const preferredX = anchorBottomRight.x - root.width;
        const preferredY = anchorBottomRight.y;
        root.x = Math.max(0, Math.min(preferredX, root.parent.width - root.width));
        root.y = preferredY + root.height <= root.parent.height
                 ? preferredY
                 : Math.max(0, preferredY - root.anchorItem.height - root.height);
    }

    onOpened: positionAtAnchor()

    // REQ-F-007: popup-only formatting (thousands separators + percentage-of-total); distinct
    // from TokenFormatter.formatTokenCount()'s K-abbreviated badge formatting (REQ-F-005), which
    // is intentionally not reused here since REQ-F-007 forbids the K-abbreviation and requires
    // exact integers instead.
    function formatWithCommas(value) {
        return value === undefined || value === null ? "" : value.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
    }

    function formatPercentage(value, total) {
        return (total === undefined || total === 0) ? "" : qsTr(" (%1%)").arg((value / total * 100).toFixed(1));
    }

    background: HnSurfaceFrame {
        surfaceRole: HnSurfaceRole.Menu
        borderColor: HoloniightPalette.borderPassive
    }

    contentItem: ColumnLayout {
        id: tokenColumn

        spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 4

        Text {
            objectName: "tokenBreakdownHeading"
            text: qsTr("Token Breakdown")
            textFormat: Text.PlainText
            font.bold: true
            color: HoloniightPalette.textPrimary
        }

        Text {
            objectName: "promptTokenRow"
            visible: root.inputTokenCount !== undefined
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            text: qsTr("Prompt Tokens: %1%2").arg(root.formatWithCommas(root.inputTokenCount))
                                              .arg(root.formatPercentage(root.inputTokenCount, root.totalTokenCount))
        }

        Text {
            objectName: "completionTokenRow"
            visible: root.outputTokenCount !== undefined
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            text: qsTr("Completion Tokens: %1%2").arg(root.formatWithCommas(root.outputTokenCount))
                                                  .arg(root.formatPercentage(root.outputTokenCount, root.totalTokenCount))
        }

        Text {
            objectName: "reasoningTokenRow"
            visible: root.reasoningTokenCount !== undefined
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            text: qsTr("Reasoning Tokens: %1%2").arg(root.formatWithCommas(root.reasoningTokenCount))
                                                 .arg(root.formatPercentage(root.reasoningTokenCount, root.totalTokenCount))
        }

        Text {
            objectName: "cacheCreationTokenRow"
            visible: root.cacheCreationTokenCount !== undefined
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            text: qsTr("Cache Creation Tokens: %1%2")
                  .arg(root.formatWithCommas(root.cacheCreationTokenCount))
                  .arg(root.formatPercentage(root.cacheCreationTokenCount, root.totalTokenCount))
        }

        Text {
            objectName: "cacheReadTokenRow"
            visible: root.cacheReadTokenCount !== undefined
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            text: qsTr("Cache Read Tokens: %1%2")
                  .arg(root.formatWithCommas(root.cacheReadTokenCount))
                  .arg(root.formatPercentage(root.cacheReadTokenCount, root.totalTokenCount))
        }

        Text {
            objectName: "totalTokenRow"
            visible: root.totalTokenCount !== undefined
            textFormat: Text.PlainText
            font.bold: true
            color: HoloniightPalette.textPrimary
            text: qsTr("Total Tokens: %1").arg(root.formatWithCommas(root.totalTokenCount))
        }
    }
}
