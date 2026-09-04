pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC2
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

HnSurfaceFrame {
    id: root

    property bool showRetryAction: true
    property bool compact: false

    readonly property bool canSubmit: !ChatViewModel.isStreaming
                                      && ChatViewModel.canSend
                                      && inputBox.text.trim().length > 0
    readonly property real composerPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)
    readonly property real editorLineHeight: editorFontMetrics.lineSpacing
    readonly property real minimumEditorHeight: editorLineHeight * 3
    readonly property real maximumEditorHeight: editorLineHeight * 8

    function submit(): void {
        if (canSubmit)
            ChatViewModel.send(inputBox.text)
    }

    function focusEditor(): void {
        inputBox.forceActiveFocus()
    }

    implicitHeight: composerLayout.implicitHeight + composerPadding * 2
    surfaceRole: HnSurfaceRole.Control
    chamferedCornersOverride: HnCornerMask.TopRight
    fillColor: HoloniightPalette.surface
    borderColor: inputBox.activeFocus ? HoloniightPalette.borderFocus : HoloniightPalette.borderPassive
    borderWidth: inputBox.activeFocus ? HnMetrics.focusBorderWidth : HnMetrics.borderWidth

    FontMetrics {
        id: editorFontMetrics

        font: inputBox.font
    }

    ColumnLayout {
        id: composerLayout

        anchors.fill: parent
        anchors.margins: root.composerPadding
        spacing: root.compact ? HnMetrics.internalSpacing(HnControlSize.Normal) / 2 : HnMetrics.internalSpacing(HnControlSize.Normal)

        ScrollView {
            id: editorScrollView

            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(root.minimumEditorHeight,
                                             Math.min(root.maximumEditorHeight, inputBox.contentHeight))
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: inputBox.contentHeight > height
                                       ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

            TextArea {
                id: inputBox

                objectName: "composerEditor"
                width: editorScrollView.contentWidth
                implicitWidth: 0
                padding: 0
                text: ChatViewModel.inputText
                color: HoloniightPalette.textPrimary
                placeholderText: qsTr("Ask anything…")
                placeholderTextColor: HoloniightPalette.textMuted
                selectionColor: HoloniightPalette.primary
                selectedTextColor: HoloniightPalette.onPrimary
                wrapMode: TextEdit.Wrap
                background: null
                Accessible.name: qsTr("Message")

                onTextChanged: {
                    if (ChatViewModel.inputText !== text)
                        ChatViewModel.inputText = text
                }

                Keys.onReturnPressed: event => {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                        return
                    }
                    event.accepted = true
                    root.submit()
                }

                Keys.onEnterPressed: event => {
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false
                        return
                    }
                    event.accepted = true
                    root.submit()
                }
            }
        }

        ChatComposerActions {
            id: composerActions

            Layout.fillWidth: true
            compact: root.compact
            showRetryAction: root.showRetryAction
            canRegenerate: ChatViewModel.canRegenerate
            canSubmit: root.canSubmit
            onRetryRequested: ChatViewModel.regenerate()
            onSubmitRequested: root.submit()
        }
    }

    Connections {
        target: ChatViewModel

        function onInputTextChanged(): void {
            if (inputBox.text !== ChatViewModel.inputText)
                inputBox.text = ChatViewModel.inputText
        }
    }

    Component.onCompleted: inputBox.forceActiveFocus()
}
