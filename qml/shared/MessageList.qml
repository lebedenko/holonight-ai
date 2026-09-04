pragma ComponentBehavior: Bound

import QtQuick
import HolonightChat
import Holonight.Core
import Holonight.Controls as HnControls

Item {
    id: root

    property real messageWidthRatio: 0.75
    readonly property alias followingLatest: messageView.followingLatest

    clip: true

    HnControls.HnLoadingState {
        anchors.centerIn: parent
        width: Math.min(parent.width, 320)
        titleText: qsTr("Loading conversation")
        descriptionText: qsTr("Restoring your messages…")
        visible: !ChatViewModel.messagesReady
    }

    BottomAnchoredListView {
        id: messageView

        objectName: "messageView"
        anchors.fill: parent
        visible: ChatViewModel.messagesReady
        clip: true
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)
        model: ChatViewModel.messages

        delegate: ChatMessageDelegate {
            id: messageDelegate

            width: ListView.view.width
            messageWidthRatio: root.messageWidthRatio
            onStopRequested: ChatViewModel.stop()
            onUserExpansionStarted: messageView.beginPreservingItemTop(messageDelegate)
            onUserExpansionFinished: messageView.finishPreservingItemTop()
        }
    }
}
