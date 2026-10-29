import QtQuick

TextEdit {
    id: root

    visible: false
    textFormat: TextEdit.PlainText

    function copyText(value: string): void {
        text = value
        selectAll()
        copy()
        deselect()
        text = ""
    }
}
