pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root

    signal newChatRequested()

    readonly property alias dropdownOpen: dropdownPopup.visible

    content: RowLayout {
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        HnAppTitle {
            Layout.fillWidth: true
            applicationName: qsTr("Quick chat")
            skipBranding: true
            iconSource: "qrc:/HolonightChat/assets/holonight-ai.svg"
        }

        RowLayout {
            id: actionsRow

            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            HnIconButton {
                id: dropdownTrigger

                sizeRole: HnControlSize.Large
                icon.source: "qrc:/HolonightChat/assets/icons/plus.svg"
                Accessible.name: qsTr("New or recent chats")
                onClicked: dropdownPopup.visible ? dropdownPopup.close() : dropdownPopup.open()
            }

            HnIconButton {
                id: workspaceButton

                sizeRole: HnControlSize.Large
                icon.source: "qrc:/HolonightChat/assets/icons/open-workspace.svg"
                Accessible.name: qsTr("Switch to workspace")
                onClicked: ChatApplication.ShowWorkspace()
            }

            HnIconButton {
                id: pinButton

                sizeRole: HnControlSize.Large
                enabled: false
                icon.source: "qrc:/HolonightChat/assets/icons/pin.svg"
                Accessible.name: qsTr("Pin conversation (not yet implemented)")
            }

            HnIconButton {
                id: closeButton

                sizeRole: HnControlSize.Large
                icon.source: "qrc:/HolonightChat/assets/icons/close.svg"
                Accessible.name: qsTr("Close quick panel")
                onClicked: ChatApplication.ClosePanel(false)
            }
        }
    }

    Popup {
        id: dropdownPopup

        parent: root
        x: root.width - width - root.horizontalPadding
        y: root.height
        width: Math.min(Math.max(220, HnMetrics.controlHeight(HnControlSize.Large) * 4),
                        root.width - root.horizontalPadding * 2)
        implicitHeight: dropdownContent.implicitHeight + 2
        padding: 1
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: HnSurfaceFrame {
            surfaceRole: HnSurfaceRole.Menu
            borderColor: HoloniightPalette.borderPassive
        }

        contentItem: ColumnLayout {
            id: dropdownContent

            spacing: 0

            HnActionDelegate {
                Layout.fillWidth: true
                title: qsTr("New chat")
                showChevron: false
                onClicked: {
                    ChatViewModel.createConversation();
                    dropdownPopup.close();
                    root.newChatRequested();
                }
            }

            HnSeparator {
                Layout.fillWidth: true
                color: HoloniightPalette.borderPassive
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                interactive: false
                clip: true
                model: ChatViewModel.conversationList

                delegate: HnListDelegate {
                    id: dropdownRow

                    required property string conversationId
                    required property int index

                    readonly property bool isActive: dropdownRow.conversationId === ChatViewModel.activeConversationId

                    width: ListView.view.width
                    visible: dropdownRow.index < 10
                    height: visible ? implicitHeight : 0
                    title: dropdownRow.title
                    checked: dropdownRow.isActive
                    onClicked: {
                        ChatViewModel.switchConversation(dropdownRow.conversationId);
                        dropdownPopup.close();
                    }
                }
            }
        }
    }
}
