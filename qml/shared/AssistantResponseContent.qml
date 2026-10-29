import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property string messageText
    required property string messageStatus
    property Item selectionViewport: null
    signal selectionScrollRequested(real deltaY)

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    MarkdownBlock {
        Layout.fillWidth: true
        markdown: root.messageText
        streaming: root.messageStatus === "streaming"
        selectionViewport: root.selectionViewport
        onSelectionScrollRequested: deltaY => root.selectionScrollRequested(deltaY)
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.messageText.length > 0
                 && (root.messageStatus === "complete" || root.messageStatus === "error"
                     || root.messageStatus === "cancelled")

        Item {
            Layout.fillWidth: true
        }

        ClipboardHelper {
            id: clipboard
        }

        HnIconButton {
            id: responseCopyButton
            objectName: "responseCopyButton"

            property bool copied: false

            sizeRole: HnControlSize.Compact
            icon.source: "qrc:/HolonightChat/assets/icons/documentation.svg"
            Accessible.name: copied ? qsTr("Copied response") : qsTr("Copy response")
            onClicked: {
                clipboard.copyText(root.messageText)
                copied = true
                responseCopyReset.restart()
            }

            Timer {
                id: responseCopyReset

                interval: 1500
                onTriggered: responseCopyButton.copied = false
            }
        }
    }
}
