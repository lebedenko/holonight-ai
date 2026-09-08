import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts

import Holonight.Core
import Holonight.Controls

Item {
    id: root

    property var toolCall: ({})

    function readablePayload(): string {
        const serialized = root.toolCall && root.toolCall.kind === "invocation"
                         ? root.toolCall.rawArgumentsJson
                         : (root.toolCall ? root.toolCall.rawResultJson : undefined);
        if (typeof serialized === "string" && serialized.length > 0) {
            return serialized;
        }

        const value = root.toolCall && root.toolCall.kind === "invocation"
                    ? root.toolCall.input
                    : (root.toolCall ? root.toolCall.result : undefined);
        if (value === undefined || value === null) {
            return qsTr("No raw data available.");
        }
        if (typeof value === "string") {
            return value;
        }
        try {
            const encoded = JSON.stringify(value, null, 2);
            return typeof encoded === "string" ? encoded : qsTr("Raw data is unavailable.");
        } catch (error) {
            return qsTr("Raw data is unavailable.");
        }
    }

    implicitWidth: Math.max(240, column.implicitWidth)
    implicitHeight: column.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Text {
            text: qsTr("Arguments / details")
            font.bold: true
            color: HoloniightPalette.textPrimary
            Layout.fillWidth: true
        }

        Controls.TextArea {
            objectName: "genericToolPayload"
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            text: root.readablePayload()
            textFormat: Controls.TextArea.PlainText
            wrapMode: Controls.TextArea.Wrap
            padding: 0
            color: HoloniightPalette.textPrimary
            font.family: HolonightTheme.monospaceFont
            background: Rectangle {
                color: HoloniightPalette.surfaceElevated
                radius: height / 2
            }
        }
    }
}
