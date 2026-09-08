import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    required property string blockId
    required property string code
    required property string language
    required property bool complete

    objectName: "chatCodeBlock:" + blockId
    spacing: 0

    readonly property var highlighter: highlighterLoader.item

    Loader {
        id: highlighterLoader
        objectName: "codeHighlighterLoader"

        active: root.complete
        sourceComponent: Component {
            CodeHighlighter {
                language: root.language
            }
        }
        onLoaded: (item as CodeHighlighter).attachTo(body.textDocument)
    }

    Connections {
        target: HoloniightPalette

        function onPaletteChanged(): void {
            if (root.highlighter && root.highlighter.highlightingActive)
                root.highlighter.refreshTheme()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 2

        Text {
            objectName: "codeLanguageLabel"
            text: root.highlighter && root.highlighter.highlightingActive ? root.language : qsTr("plain text")
            textFormat: Text.PlainText
            color: HoloniightPalette.textSecondary
            font.family: HolonightTheme.monospaceFont
        }

        Item {
            Layout.fillWidth: true
        }

        Controls.Button {
            id: copyButton

            property bool copied: false

            text: copied ? qsTr("Copied") : qsTr("Copy")

            onClicked: {
                body.selectAll()
                body.copy()
                body.deselect()
                copied = true
                resetTimer.restart()
            }

            Timer {
                id: resetTimer

                interval: 1500
                onTriggered: copyButton.copied = false
            }
        }
    }

    Flickable {
        Layout.fillWidth: true
        Layout.preferredHeight: body.implicitHeight
        contentWidth: body.implicitWidth
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        HnSurfaceFrame {
            width: Math.max(parent.width, body.implicitWidth)
            height: body.implicitHeight
            surfaceRole: HnSurfaceRole.Card
            fillColor: HoloniightPalette.surfaceElevated
            borderColor: HoloniightPalette.borderSubtle

            TextEdit {
                id: body
                objectName: "codeBlockBody"

                text: root.code
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                readOnly: true
                selectByMouse: true
                font.family: HolonightTheme.monospaceFont
                color: HoloniightPalette.textPrimary
            }
        }
    }
}
