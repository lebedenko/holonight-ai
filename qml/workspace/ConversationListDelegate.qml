pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC2
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import Holonight

HnNavigationDelegate {
    id: root

    required property var model
    required property string conversationId
    required property string updatedAt
    required property bool titleGenerationInProgress
    required property bool pinned
    property bool isActive: false
    property string filterText: ""
    property bool showOnlyPinned: false

    signal activateRequested()
    signal renameRequested(string newTitle)
    signal deleteConfirmed()
    signal pinToggleRequested()

    property bool editing: false
    property bool confirmingDelete: false

    readonly property bool matchesFilter: root.filterText.length === 0
        || root.title.toLowerCase().includes(root.filterText.toLowerCase())
    readonly property bool matchesSection: root.showOnlyPinned === root.pinned

    title: root.model.title
    checked: root.isActive
    visible: root.matchesFilter && root.matchesSection
    height: root.visible ? root.implicitHeight : 0
    showTitleToolTipWhenElided: true
    Accessible.description: root.titleGenerationInProgress ? qsTr("Generating conversation title") : ""
    contentItem.visible: !root.editing && !root.confirmingDelete
    leadingContent: Component {
        HnIcon {
            source: "qrc:/HolonightChat/assets/icons/chat.svg"
            size: 16
            iconState: HnIcon.Muted
        }
    }
    trailingContent: Component {
        Item {
            implicitWidth: Math.max(updatedAtLabel.implicitWidth, namingStatus.implicitWidth, actionsButton.implicitWidth)
            implicitHeight: Math.max(updatedAtLabel.implicitHeight, namingStatus.implicitHeight, actionsButton.implicitHeight)

            Text {
                id: updatedAtLabel
                objectName: "updatedAtLabel"

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.titleGenerationInProgress && !root.hovered && !actionsMenu.visible
                text: root.updatedAt
                textFormat: Text.PlainText
                color: HoloniightPalette.textMuted
            }

            RowLayout {
                id: namingStatus
                objectName: "namingStatus"

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                visible: opacity > 0
                opacity: root.titleGenerationInProgress && !root.hovered && !actionsMenu.visible ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: 120 }
                }

                HnIcon {
                    id: namingIcon
                    source: "qrc:/HolonightChat/assets/icons/refresh.svg"
                    size: 14
                    iconState: HnIcon.Active

                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 900
                        loops: Animation.Infinite
                        running: namingStatus.opacity > 0
                    }
                }

                Text {
                    objectName: "namingLabel"
                    text: qsTr("Naming…")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textMuted
                }
            }

            HnIconButton {
                id: actionsButton

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hovered || actionsMenu.visible
                icon.source: "qrc:/qt/qml/Holonight/Controls/assets/more-vertical.svg"
                Accessible.name: qsTr("Conversation actions")
                onClicked: actionsMenu.popup()

                Menu {
                    id: actionsMenu

                    y: parent.height

                    MenuItem {
                        text: qsTr("Rename")
                        hoverEnabled: true
                        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/edit.svg"
                        onTriggered: {
                            root.editing = true;
                            renameField.text = root.title;
                            renameField.forceActiveFocus();
                            renameField.selectAll();
                        }
                    }

                    MenuItem {
                        objectName: "pinMenuItem"
                        text: root.pinned ? qsTr("Unpin") : qsTr("Pin")
                        hoverEnabled: true
                        icon.source: "qrc:/HolonightChat/assets/icons/pin.svg"
                        onTriggered: root.pinToggleRequested()
                    }

                    MenuItem {
                        text: qsTr("Delete")
                        hoverEnabled: true
                        icon.source: "qrc:/qt/qml/Holonight/Controls/assets/delete.svg"
                        onTriggered: root.confirmingDelete = true
                    }
                }
            }
        }
    }

    onClicked: {
        if (!root.editing && !root.confirmingDelete)
            root.activateRequested();
    }
    onDoubleClicked: {
        if (!root.editing && !root.confirmingDelete) {
            root.editing = true;
            renameField.text = root.title;
            renameField.forceActiveFocus();
            renameField.selectAll();
        }
    }

    TextField {
        id: renameField

        anchors.left: root.contentItem.left
        anchors.right: root.contentItem.right
        anchors.top: root.top
        anchors.bottom: root.bottom
        anchors.topMargin: HnMetrics.horizontalPadding(HnControlSize.Normal) / 2
        anchors.bottomMargin: HnMetrics.horizontalPadding(HnControlSize.Normal) / 2
        visible: root.editing

        onEditingFinished: {
            root.editing = false;
            if (renameField.text.length > 0 && renameField.text !== root.title) {
                root.renameRequested(renameField.text);
            }
        }
        Keys.onEscapePressed: {
            root.editing = false;
        }
    }

    Item {
        anchors.fill: root.contentItem
        visible: root.confirmingDelete

        Text {
            anchors.left: parent.left
            anchors.right: confirmationActions.left
            anchors.rightMargin: root.semanticSpacing
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Delete?")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: HoloniightPalette.textPrimary
        }

        RowLayout {
            id: confirmationActions

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: root.semanticSpacing

            Button {
                text: qsTr("Yes")
                onClicked: {
                    root.confirmingDelete = false;
                    root.deleteConfirmed();
                }
            }

            Button {
                text: qsTr("No")
                onClicked: root.confirmingDelete = false
            }
        }
    }
}
