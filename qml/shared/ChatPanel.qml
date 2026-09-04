import QtQuick
import QtQuick.Layouts
import Holonight.Core

ColumnLayout {
    id: root

    property bool compact: false
    property bool showHeader: true
    property bool showRetryAction: true
    property real messageWidthRatio: compact ? 0.9 : 0.75

    function focusComposer(): void {
        composer.focusEditor()
    }

    spacing: compact ? HnMetrics.internalSpacing(HnControlSize.Normal) / 2 : HnMetrics.internalSpacing(HnControlSize.Normal)

    ChatHeader {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
        visible: root.showHeader
        compact: root.compact
    }

    ChatNoticeStack {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
    }

    MessageList {
        Layout.fillWidth: true
        Layout.fillHeight: true
        messageWidthRatio: root.messageWidthRatio
    }

    StreamingStatusBar {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
    }

    ChatComposer {
        id: composer

        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
        compact: root.compact
        showRetryAction: root.showRetryAction
    }
}
