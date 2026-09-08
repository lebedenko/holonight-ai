import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

RowLayout {
    id: root

    required property bool compact
    required property bool showRetryAction
    required property bool canRegenerate
    required property bool canSubmit

    signal retryRequested
    signal submitRequested

    objectName: "composerActions"
    spacing: compact ? HnMetrics.internalSpacing(HnControlSize.Normal) / 2 : HnMetrics.internalSpacing(HnControlSize.Normal)

    Controls.Button {
        objectName: "attachmentButton"
        visible: !root.compact
        enabled: false
        implicitWidth: implicitHeight
        display: AbstractButton.IconOnly
        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/paperclip.svg"
        icon.color: enabled ? HoloniightPalette.textPrimary : HoloniightPalette.textDisabled
        Accessible.name: qsTr("Attach a file")
        Accessible.description: qsTr("File attachments are not available yet")
    }

    Controls.Button {
        objectName: "contextButton"
        visible: !root.compact
        enabled: false
        text: qsTr("Context")
        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
        icon.color: enabled ? HoloniightPalette.textPrimary : HoloniightPalette.textDisabled
        Accessible.name: qsTr("Choose context folder")
        Accessible.description: qsTr("Context folder selection is not available yet")
    }

    Controls.Button {
        objectName: "toolsButton"
        visible: !root.compact
        enabled: false
        text: qsTr("Tools")
        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/wrench.svg"
        icon.color: enabled ? HoloniightPalette.textPrimary : HoloniightPalette.textDisabled
        Accessible.name: qsTr("Choose tools")
        Accessible.description: qsTr("Tools are not available yet")
    }

    Item {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }

    Controls.Button {
        objectName: "retryButton"
        visible: root.showRetryAction && root.canRegenerate
        text: qsTr("Retry")
        onClicked: root.retryRequested()
    }

    Text {
        objectName: "composerHint"
        visible: !root.compact
        Layout.minimumWidth: 0
        text: qsTr("Enter to send · Shift+Enter for new line")
        textFormat: Text.PlainText
        color: HoloniightPalette.textMuted
        elide: Text.ElideRight
    }

    Controls.Button {
        objectName: "sendButton"
        highlighted: true
        text: qsTr("Send")
        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/send.svg"
        icon.color: enabled ? HoloniightPalette.onPrimary : HoloniightPalette.textDisabled
        enabled: root.canSubmit
        Accessible.description: qsTr("Send the current message")
        onClicked: root.submitRequested()
    }
}
