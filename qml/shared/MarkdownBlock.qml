import QtQuick
import Holonight.Core

Item {
    id: root

    required property string markdown
    readonly property string renderedMarkdown: escapeUnsupportedContainers(markdown)

    implicitHeight: body.implicitHeight

    function escapeUnsupportedContainers(source: string): string {
        // Qt's Markdown renderer does not support disclosure containers. More importantly, a
        // fenced code block inside one is rendered by a separate delegate, leaving unmatched raw
        // HTML tags on either side that can swallow otherwise valid Markdown. Keep the tags and
        // their contents visible as text; `markdown` remains untouched for full-response copy.
        return source.replace(/<(\/?)(details|summary)(?:\s[^>]*)?>/gi, "&lt;$1$2&gt;")
    }

    TextEdit {
        id: body

        objectName: "markdownBlockBody"

        width: parent.width
        text: width > 0 ? root.renderedMarkdown : ""
        textFormat: TextEdit.MarkdownText
        wrapMode: TextEdit.Wrap
        color: HoloniightPalette.textPrimary
        readOnly: true
        selectByMouse: true

        onLinkActivated: link => Qt.openUrlExternally(link)
    }
}
