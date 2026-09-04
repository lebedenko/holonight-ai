import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property var contentModel
    required property string messageStatus

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    function blockAt(index: int): var {
        return blockRepeater.itemAt(index)
    }

    Repeater {
        id: blockRepeater
        objectName: "assistantResponseBlockRepeater"

        model: root.contentModel

        delegate: Loader {
            id: blockLoader

            required property string blockId
            required property int type
            required property string text
            required property string language
            required property bool complete

            objectName: "assistantResponseBlock:" + blockId
            Layout.fillWidth: true
            active: width > 0
            sourceComponent: active
                             ? (blockLoader.type === ContentBlockType.Code ? codeDelegate : markdownDelegate)
                             : null

            Component {
                id: markdownDelegate

                MarkdownBlock {
                    markdown: blockLoader.text
                }
            }

            Component {
                id: codeDelegate

                ChatCodeBlock {
                    blockId: blockLoader.blockId
                    code: blockLoader.text
                    language: blockLoader.language
                    complete: blockLoader.complete
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.contentModel && root.contentModel.rawMarkdown.length > 0
                 && (root.messageStatus === "complete" || root.messageStatus === "error"
                     || root.messageStatus === "cancelled")

        Item {
            Layout.fillWidth: true
        }

        TextEdit {
            id: rawResponseCopySource

            visible: false
            text: root.contentModel ? root.contentModel.rawMarkdown : ""
            textFormat: TextEdit.PlainText
        }

        HnIconButton {
            id: responseCopyButton
            objectName: "responseCopyButton"

            property bool copied: false

            sizeRole: HnControlSize.Compact
            icon.source: "qrc:/HolonightChat/assets/icons/documentation.svg"
            Accessible.name: copied ? qsTr("Copied response") : qsTr("Copy response")
            onClicked: {
                rawResponseCopySource.selectAll()
                rawResponseCopySource.copy()
                rawResponseCopySource.deselect()
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
