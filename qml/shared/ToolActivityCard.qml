import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property var toolCall
    required property date createdAt
    property real maximumWidthRatio: 0.75

    ToolRendererRegistry {
        id: toolRendererRegistry
    }

    readonly property bool isInvocation: root.toolCall.kind === "invocation"
    readonly property bool isError: root.toolCall.isError === true
    readonly property bool isAwaitingApproval: root.toolCall.status === "awaiting_approval"
    readonly property bool isRunning: root.toolCall.status === "running"
    readonly property bool isFinalFailure: root.toolCall.status === "failed" || root.toolCall.status === "denied"
                                || root.toolCall.status === "cancelled"
    readonly property bool canStop: root.toolCall.canCancel === true
    readonly property bool defaultExpanded: root.isRunning || isAwaitingApproval || isFinalFailure
    readonly property string toolTitle: root.toolCall.toolTitle || root.toolCall.toolName || qsTr("Tool")
    readonly property string toolSummary: root.toolCall.summary || (root.isInvocation
        ? qsTr("%1 requested").arg(root.toolTitle)
        : (root.isError ? qsTr("%1 failed").arg(root.toolTitle) : qsTr("%1 complete").arg(root.toolTitle)))
    readonly property string rendererKey: root.toolCall.rendererKey || "generic"
    readonly property string rawArgumentsText: root.rawText("rawArgumentsJson", "input")
    readonly property string rawResultText: root.rawText("rawResultJson", "result")
    readonly property bool hasRawData: root.rawArgumentsText.length > 0 || root.rawResultText.length > 0

    property bool expanded: defaultExpanded
    property bool rawExpanded: false
    property bool hasUserExpandedChoice: false
    property string lastToolCallId: ""

    implicitHeight: messageFrame.height

    signal stopRequested(string toolUseId)
    signal userExpansionStarted()
    signal userExpansionFinished()

    function rawText(serializedKey: string, fallbackKey: string): string {
        const serialized = root.toolCall ? root.toolCall[serializedKey] : undefined;
        if (typeof serialized === "string" && serialized.length > 0) {
            return serialized;
        }

        const fallback = root.toolCall ? root.toolCall[fallbackKey] : undefined;
        if (fallback === undefined || fallback === null) {
            return "";
        }
        if (typeof fallback === "string") {
            return fallback;
        }

        try {
            const encoded = JSON.stringify(fallback, null, 2);
            return typeof encoded === "string" ? encoded : qsTr("Raw data is unavailable.");
        } catch (error) {
            return qsTr("Raw data is unavailable.");
        }
    }

    function applyDefaultExpansion(): void {
        if (!hasUserExpandedChoice) {
            expanded = defaultExpanded;
        }
    }

    Component.onCompleted: {
        root.lastToolCallId = root.toolCall && root.toolCall.toolUseId ? root.toolCall.toolUseId : "";
        applyDefaultExpansion();
    }
    onToolCallChanged: {
        const currentId = root.toolCall ? root.toolCall.toolUseId : "";
        if (currentId !== root.lastToolCallId) {
            root.hasUserExpandedChoice = false;
            root.lastToolCallId = currentId;
        }
        applyDefaultExpansion();
    }

    HnSurfaceFrame {
        id: messageFrame
        objectName: "toolActivityCardFrame"

        Accessible.name: qsTr("%1 tool activity").arg(root.toolTitle)
        Accessible.description: root.toolSummary
        anchors.top: parent.top
        anchors.left: parent.left
        width: Math.max(0, root.width * Math.min(1, Math.max(0, root.maximumWidthRatio)))
        surfaceRole: HnSurfaceRole.Card
        chamferedCornersOverride: HnCornerMask.TopRight
        fillColor: HoloniightPalette.surface
        borderColor: root.isError ? HoloniightPalette.borderUrgent : (root.isRunning ? HoloniightPalette.borderActive
                                                                                  : HoloniightPalette.borderSubtle)
        borderWidth: HnMetrics.borderWidth
        height: content.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 4

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

            RowLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal) / 2

                Rectangle {
                    Layout.preferredWidth: HnMetrics.controlHeight(HnControlSize.Normal) * 0.6
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: root.isError ? HoloniightPalette.error : (root.isRunning ? HoloniightPalette.accentBlue
                                                                                : HoloniightPalette.accentCyan)

                    Text {
                        anchors.centerIn: parent
                        text: "T"
                        textFormat: Text.PlainText
                        color: HoloniightPalette.onPrimary
                        font.bold: true
                    }
                }

                Text {
                    text: root.toolTitle
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textPrimary
                    font.bold: true
                    Layout.maximumWidth: content.width * 0.35
                    elide: Text.ElideMiddle
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    text: root.toolSummary
                    color: root.isError ? HoloniightPalette.error : HoloniightPalette.textMuted
                    font.pixelSize: 12
                    Layout.preferredWidth: content.width * 0.45
                    elide: Text.ElideRight
                }

                Text {
                    visible: !root.expanded
                    text: Qt.formatTime(root.createdAt, "HH:mm")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textMuted
                }

                Controls.Button {
                    id: disclosureButton
                    objectName: "toolActivityDisclosure"
                    text: root.expanded ? qsTr("Hide details") : qsTr("Show details")
                    Accessible.name: text
                    onClicked: {
                        root.userExpansionStarted();
                        root.expanded = !root.expanded;
                        root.hasUserExpandedChoice = true;
                        Qt.callLater(root.userExpansionFinished);
                    }
                }

                Controls.Button {
                    objectName: "toolActivityStop"
                    visible: root.canStop && root.isRunning
                    text: qsTr("Stop")
                    Accessible.name: qsTr("Stop tool execution")
                    onClicked: root.stopRequested(root.toolCall.toolUseId)
                }
            }

            Text {
                objectName: "toolActivityStatus"
                text: root.toolCall.status
                textFormat: Text.PlainText
                color: HoloniightPalette.textMuted
                Layout.fillWidth: true
                visible: root.expanded
            }

            Text {
                objectName: "toolActivityFunction"
                text: root.toolCall.functionName
                textFormat: Text.PlainText
                color: HoloniightPalette.textMuted
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: root.expanded && root.toolCall.functionName.length > 0
            }

            Text {
                objectName: "toolActivityTime"
                text: Qt.formatTime(root.createdAt, "HH:mm")
                color: HoloniightPalette.textMuted
                visible: root.expanded
            }

            Loader {
                id: rendererLoader
                objectName: "toolActivityRendererLoader"
                Layout.fillWidth: true
                Layout.preferredHeight: active && item ? (item as Item).implicitHeight : 0
                active: root.expanded
                visible: active
                sourceComponent: root.expanded ? toolRendererRegistry.componentFor(root.rendererKey) : null

                property var toolCall: root.toolCall

                onLoaded: {
                    item.toolCall = root.toolCall;
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.expanded && root.hasRawData
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Controls.Button {
                    objectName: "toolActivityRawDisclosure"
                    text: root.rawExpanded ? qsTr("Hide raw") : qsTr("View raw")
                    Accessible.name: text
                    onClicked: root.rawExpanded = !root.rawExpanded
                }

                Controls.Button {
                    objectName: "toolActivityCopyResult"
                    visible: root.rawResultText.length > 0
                    text: qsTr("Copy result")
                    Accessible.name: qsTr("Copy complete tool result")
                    onClicked: {
                        rawResult.selectAll();
                        rawResult.copy();
                        rawResult.deselect();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                objectName: "toolActivityRawDetails"
                Layout.fillWidth: true
                visible: root.expanded && root.rawExpanded && root.hasRawData
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    visible: root.rawArgumentsText.length > 0
                    text: qsTr("Arguments")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textPrimary
                    font.bold: true
                }

                Controls.TextArea {
                    id: rawArguments

                    objectName: "toolActivityRawArguments"
                    visible: root.rawArgumentsText.length > 0
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: root.rawArgumentsText
                    textFormat: Controls.TextArea.PlainText
                    wrapMode: Controls.TextArea.Wrap
                    color: HoloniightPalette.textPrimary
                    font.family: HolonightTheme.monospaceFont
                    Accessible.name: qsTr("Raw tool arguments")
                }

                Text {
                    visible: root.rawResultText.length > 0
                    text: qsTr("Result")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textPrimary
                    font.bold: true
                }

                Controls.TextArea {
                    id: rawResult

                    objectName: "toolActivityRawResult"
                    visible: root.rawResultText.length > 0
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: root.rawResultText
                    textFormat: Controls.TextArea.PlainText
                    wrapMode: Controls.TextArea.Wrap
                    color: HoloniightPalette.textPrimary
                    font.family: HolonightTheme.monospaceFont
                    Accessible.name: qsTr("Raw tool result")
                }
            }
        }
    }
}
