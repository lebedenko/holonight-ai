import QtQuick
import QtQuick.Controls as Controls

ListView {
    id: root

    property bool followingLatest: true
    property Item preservedItem: null
    property real preservedItemTop: 0
    readonly property real discreteWheelStep: 100

    function showLatest(): void {
        followingLatest = true
        positionViewAtBeginning()
    }

    function beginPreservingItemTop(item: Item): void {
        preservedItem = item
        preservedItemTop = item.mapToItem(root, 0, 0).y
        preserveSettleTimer.stop()
    }

    function finishPreservingItemTop(): void {
        restorePreservedItemTop()
        preserveSettleTimer.restart()
    }

    function restorePreservedItemTop(): void {
        if (!preservedItem)
            return
        const currentTop = preservedItem.mapToItem(root, 0, 0).y
        contentY += currentTop - preservedItemTop
    }

    function handleDiscreteWheel(angleDelta: real, phase: int): bool {
        if (phase !== Qt.NoScrollPhase || angleDelta === 0 || contentHeight <= height)
            return false

        // Wayland can expose a high-resolution mouse wheel through both angleDelta and a much
        // smaller pixelDelta. Flickable then treats it like a touchpad gesture. Preserve ordinary
        // notch distance, but amplify the fine angle deltas emitted by a free-spinning wheel.
        const highResolutionBoost = Math.abs(angleDelta) < 120 ? 8 : 1
        const distance = angleDelta / 120 * discreteWheelStep * highResolutionBoost
        transcriptScrollBar.stepSize = Math.min(1, Math.abs(distance) / contentHeight)
        if (distance > 0)
            transcriptScrollBar.decrease()
        else
            transcriptScrollBar.increase()
        return true
    }

    verticalLayoutDirection: ListView.BottomToTop
    // Retain fast direct-manipulation flicks; discrete mouse wheels are handled separately below.
    maximumFlickVelocity: 30000
    // Rich assistant rows can be several viewport heights tall. Preload a bounded region around
    // the viewport so adjacent message heights are known before the user reaches their boundary;
    // this also avoids first-exposure layout work during ordinary scrolling.
    cacheBuffer: Math.max(height * 2, 8192)

    Controls.ScrollBar.vertical: Controls.ScrollBar {
        id: transcriptScrollBar

        objectName: "transcriptScrollBar"
        policy: Controls.ScrollBar.AlwaysOn
    }

    WheelHandler {
        target: null
        onWheel: event => {
            // Phased pixel scrolling is a touchpad gesture and remains on Flickable's native path.
            event.accepted = root.handleDiscreteWheel(event.angleDelta.y, event.phase)
        }
    }

    onMovementStarted: followingLatest = false
    onMovementEnded: {
        if (atYBeginning)
            followingLatest = true
    }
    onAtYBeginningChanged: {
        if (atYBeginning)
            followingLatest = true
    }

    Connections {
        target: root.preservedItem
        ignoreUnknownSignals: true

        function onHeightChanged(): void {
            Qt.callLater(root.restorePreservedItemTop)
            preserveSettleTimer.restart()
        }
    }

    Timer {
        id: preserveSettleTimer

        interval: 50
        onTriggered: {
            root.restorePreservedItemTop()
            root.preservedItem = null
        }
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onRowsInserted(): void {
            Qt.callLater(root.showLatest)
        }

        function onModelReset(): void {
            Qt.callLater(root.showLatest)
        }
    }
}
