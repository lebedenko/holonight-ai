pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    objectName: "chatMessageDelegate"

    required property string messageId
    required property string role
    required property string text
    required property string status
    required property date createdAt
    required property string modelName
    required property string providerId
    required property string providerType
    required property string providerName
    required property var contentBlocks
    required property var inputTokenCount
    required property var outputTokenCount
    required property var reasoningTokenCount
    required property var cacheCreationTokenCount
    required property var cacheReadTokenCount
    required property var totalTokenCount
    required property var durationMs
    required property var toolCall
    required property real messageWidthRatio

    height: messageLoader.item ? (messageLoader.item as Item).implicitHeight : 0

    signal stopRequested(string toolUseId)
    signal userExpansionStarted()
    signal userExpansionFinished()

    function pickComponent(): Component {
        if (root.toolCall !== undefined && root.toolCall !== null) {
            return toolCallCardComponent;
        }
        return root.role === "user" ? userMessageComponent : messageBubbleComponent;
    }

    Loader {
        id: messageLoader

        objectName: "chatMessageLoader"
        width: parent.width
        active: width > 0
        sourceComponent: active ? root.pickComponent() : null
    }

    Component {
        id: userMessageComponent

        UserMessageCard {
            objectName: "chatUserMessageCard"
            width: root.width
            messageText: root.text
            createdAt: root.createdAt
            maximumWidthRatio: root.messageWidthRatio
        }
    }

    Component {
        id: messageBubbleComponent

        MessageBubble {
            objectName: "chatMessageBubble"
            width: root.width
            messageRole: root.role
            messageText: root.text
            messageStatus: root.status
            modelName: root.modelName
            providerId: root.providerId
            providerType: root.providerType
            providerName: root.providerName
            createdAt: root.createdAt
            maximumWidthRatio: root.messageWidthRatio
            contentBlocks: root.contentBlocks
            inputTokenCount: root.inputTokenCount
            outputTokenCount: root.outputTokenCount
            reasoningTokenCount: root.reasoningTokenCount
            cacheCreationTokenCount: root.cacheCreationTokenCount
            cacheReadTokenCount: root.cacheReadTokenCount
            totalTokenCount: root.totalTokenCount
            durationMs: root.durationMs
        }
    }

    Component {
        id: toolCallCardComponent

        ToolActivityCard {
            objectName: "chatToolActivityCard"
            width: root.width
            toolCall: root.toolCall
            createdAt: root.createdAt
            maximumWidthRatio: root.messageWidthRatio
            onStopRequested: toolUseId => root.stopRequested(toolUseId)
            onUserExpansionStarted: root.userExpansionStarted()
            onUserExpansionFinished: root.userExpansionFinished()
        }
    }
}
