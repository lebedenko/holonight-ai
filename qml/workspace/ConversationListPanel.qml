pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

HnSurfaceFrame {
    id: root

    surfaceRole: HnSurfaceRole.Panel

    property alias searchText: searchField.text
    property int pinnedVisibleCount: 0
    property int recentVisibleCount: 0

    signal settingsToggleRequested()
    signal newChatRequested()

    function updateVisibleCounts(): void {
        root.pinnedVisibleCount = ChatViewModel.conversationList.visibleCount(true, root.searchText);
        root.recentVisibleCount = ChatViewModel.conversationList.visibleCount(false, root.searchText);
    }

    onSearchTextChanged: root.updateVisibleCounts()
    Component.onCompleted: root.updateVisibleCounts()

    Connections {
        target: ChatViewModel.conversationList

        function onModelReset(): void { root.updateVisibleCounts() }
        function onRowsInserted(): void { root.updateVisibleCounts() }
        function onRowsRemoved(): void { root.updateVisibleCounts() }
        function onRowsMoved(): void { root.updateVisibleCounts() }
        function onDataChanged(): void { root.updateVisibleCounts() }
    }

    Shortcut {
        sequence: "/"
        enabled: !searchField.activeFocus
        onActivated: searchField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HnHeaderBar {
            Layout.fillWidth: true
            dividerVisible: false
            content: HnAppTitle {
                applicationName: qsTr("AI")
                iconSource: "qrc:/HolonightChat/assets/holonight-ai.svg"
                iconTinted: false
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: HoloniightPalette.controlPadding * 2
            spacing: HoloniightPalette.controlPadding * 2

            Button {
                Layout.fillWidth: true
                highlighted: true
                text: qsTr("New chat")
                icon.source: "qrc:/HolonightChat/assets/icons/plus.svg"
                Accessible.name: text
                onClicked: {
                    ChatViewModel.createConversation();
                    root.searchText = "";
                    root.newChatRequested();
                }
            }

            HnSearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: qsTr("Search conversations")
                Accessible.name: qsTr("Search conversations")
                trailingContent: Component {
                    Text {
                        text: "/"
                        textFormat: Text.PlainText
                        color: HoloniightPalette.textMuted
                        Accessible.ignored: true
                    }
                }
            }

            HnSectionHeader {
                id: pinnedSectionHeader

                Layout.fillWidth: true
                Layout.preferredHeight: visible ? implicitHeight : 0
                visible: root.pinnedVisibleCount > 0
                titleText: qsTr("Pinned")
                dividerVisible: false
                leadingContent: Component {
                    HnIcon {
                        source: "qrc:/HolonightChat/assets/icons/pin.svg"
                        size: 16
                        iconState: HnIcon.Muted
                    }
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                Layout.maximumHeight: root.height / 2
                interactive: contentHeight > height
                clip: true
                spacing: 0
                model: ChatViewModel.conversationList
                currentIndex: -1

                delegate: ConversationListDelegate {
                    id: pinnedDelegate

                    width: ListView.view.width
                    isActive: pinnedDelegate.conversationId === ChatViewModel.activeConversationId
                    filterText: root.searchText
                    pinned: model.pinned
                    showOnlyPinned: true

                    onActivateRequested: ChatViewModel.switchConversation(pinnedDelegate.conversationId)
                    onRenameRequested: newTitle => ChatViewModel.renameConversation(pinnedDelegate.conversationId, newTitle)
                    onDeleteConfirmed: ChatViewModel.deleteConversation(pinnedDelegate.conversationId)
                    onPinToggleRequested: pinnedDelegate.pinned
                        ? ChatViewModel.unpinConversation(pinnedDelegate.conversationId)
                        : ChatViewModel.pinConversation(pinnedDelegate.conversationId)
                }
            }

            HnSectionHeader {
                id: recentSectionHeader

                Layout.fillWidth: true
                Layout.preferredHeight: visible ? implicitHeight : 0
                visible: root.recentVisibleCount > 0
                titleText: qsTr("Recent")
                dividerVisible: false
                leadingContent: Component {
                    HnIcon {
                        source: "qrc:/HolonightChat/assets/icons/recent.svg"
                        size: 16
                        iconState: HnIcon.Muted
                    }
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                model: ChatViewModel.conversationList
                currentIndex: -1

                delegate: ConversationListDelegate {
                    id: delegate

                    width: ListView.view.width
                    isActive: delegate.conversationId === ChatViewModel.activeConversationId
                    filterText: root.searchText
                    pinned: model.pinned
                    showOnlyPinned: false

                    onActivateRequested: ChatViewModel.switchConversation(delegate.conversationId)
                    onRenameRequested: newTitle => ChatViewModel.renameConversation(delegate.conversationId, newTitle)
                    onDeleteConfirmed: ChatViewModel.deleteConversation(delegate.conversationId)
                    onPinToggleRequested: delegate.pinned
                        ? ChatViewModel.unpinConversation(delegate.conversationId)
                        : ChatViewModel.pinConversation(delegate.conversationId)
                }
            }

            HnSeparator {
                Layout.fillWidth: true
                Layout.leftMargin: -HoloniightPalette.controlPadding * 2
                Layout.rightMargin: -HoloniightPalette.controlPadding * 2
                color: HoloniightPalette.borderPassive
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: HoloniightPalette.controlPadding * 2

                HnIconButton {
                    Layout.fillWidth: true
                    enabled: false
                    icon.source: "qrc:/HolonightChat/assets/icons/dashboard.svg"
                    Accessible.name: qsTr("Dashboard (not yet implemented)")
                }

                HnIconButton {
                    Layout.fillWidth: true
                    enabled: false
                    icon.source: "qrc:/HolonightChat/assets/icons/documentation.svg"
                    Accessible.name: qsTr("Documentation (not yet implemented)")
                }

                HnIconButton {
                    Layout.fillWidth: true
                    enabled: false
                    icon.source: "qrc:/HolonightChat/assets/icons/code.svg"
                    Accessible.name: qsTr("Code tools (not yet implemented)")
                }

                HnIconButton {
                    Layout.fillWidth: true
                    icon.source: "qrc:/HolonightChat/assets/icons/settings.svg"
                    Accessible.name: qsTr("Settings")
                    onClicked: root.settingsToggleRequested()
                }

                HnIconButton {
                    Layout.fillWidth: true
                    enabled: false
                    icon.source: "qrc:/HolonightChat/assets/icons/help.svg"
                    Accessible.name: qsTr("Help (not yet implemented)")
                }
            }
        }
    }
}
