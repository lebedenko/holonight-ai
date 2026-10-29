pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QMarkdown 1.18
import Holonight.Core

MarkdownView {
    id: root

    objectName: "markdownBlockBody"
    property bool streaming: false

    streamingEnabled: streaming
    gfmEnabled: true
    syntaxHighlightingEnabled: true
    selectionEnabled: true
    codeBlockWrapEnabled: false
    codeBlockLineNumbersVisible: false
    codeBlockHeightLimitEnabled: true
    mathEnabled: false
    inlineMathEnabled: false
    footnotesEnabled: false

    onLinkActivated: destination => Qt.openUrlExternally(destination)
    onCopyRequested: text => clipboard.copyText(text)
    onCodeBlockCopyRequested: text => clipboard.copyText(text)

    ClipboardHelper {
        id: clipboard
    }

    style: MarkdownStyle {
        preset: HoloniightPalette.surface.hslLightness < 0.5 ? MarkdownStyle.PolishedDark : MarkdownStyle.PolishedLight
        bodyColor: HoloniightPalette.textPrimary
        h1Color: HoloniightPalette.textPrimary
        h2Color: HoloniightPalette.textPrimary
        h3Color: HoloniightPalette.textPrimary
        h4Color: HoloniightPalette.textPrimary
        h5Color: HoloniightPalette.textPrimary
        h6Color: HoloniightPalette.textPrimary
        headings.h1.separatorColor: HoloniightPalette.borderSubtle
        headings.h2.separatorColor: HoloniightPalette.borderSubtle
        linkColor: HoloniightPalette.primary
        thematicBreakColor: HoloniightPalette.borderSubtle
        quoteRuleColor: HoloniightPalette.borderSubtle
        selectionBackgroundColor: HoloniightPalette.primary
        inlineCode.font.family: HolonightTheme.monospaceFont
        inlineCode.foreground: HoloniightPalette.textPrimary
        inlineCode.backgroundColor: HoloniightPalette.surfaceElevated
        codeBlock.font.family: HolonightTheme.monospaceFont
        codeBlock.color: HoloniightPalette.textPrimary
        codeBlock.backgroundColor: HoloniightPalette.surfaceElevated
        codeBlock.borderColor: HoloniightPalette.borderSubtle
        table.headerForeground: HoloniightPalette.textPrimary
        table.headerBackground: HoloniightPalette.surfaceElevated
        table.bodyBackground: HoloniightPalette.surface
        table.alternateRowBackground: HoloniightPalette.surface
        table.borderColor: HoloniightPalette.borderSubtle
    }

    syntaxTheme: SyntaxTheme {
        keywordColor: HoloniightPalette.accentViolet
        typeColor: HoloniightPalette.accentCyan
        functionColor: HoloniightPalette.accentBlue
        variableColor: HoloniightPalette.textPrimary
        builtInColor: HoloniightPalette.accentCyan
        stringColor: HoloniightPalette.success
        numberColor: HoloniightPalette.accentYellow
        commentColor: HoloniightPalette.textMuted
        preprocessorColor: HoloniightPalette.accentViolet
        operatorColor: HoloniightPalette.textPrimary
        constantColor: HoloniightPalette.accentYellow
        errorColor: HoloniightPalette.error
    }

    codeBlockActions: Component {
        RowLayout {
            id: actions

            objectName: "codeBlockActions"
            required property string codeText
            required property var requestCopy
            required property string language
            required property string languageId
            required property string languageName
            required property string infoString
            required property bool fenced

            Text {
                objectName: "codeLanguageLabel"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                text: actions.languageName.length > 0 ? actions.languageName : qsTr("plain text")
                textFormat: Text.PlainText
                color: HoloniightPalette.textSecondary
                font.family: HolonightTheme.monospaceFont
            }

            Controls.Button {
                id: copyButton

                objectName: "codeCopyButton"
                property bool copied: false
                text: copied ? qsTr("Copied") : qsTr("Copy")
                onClicked: {
                    actions.requestCopy()
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
    }
}
