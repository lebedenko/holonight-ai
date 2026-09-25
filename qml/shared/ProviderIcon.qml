pragma ComponentBehavior: Bound

import QtQuick
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    required property string providerType
    property bool framed: true

    readonly property var _table: ({
        "ollama":    "qrc:/HolonightChat/assets/providers/ollama.svg",
        "openai":    "qrc:/HolonightChat/assets/providers/openai.svg",
        "anthropic": "qrc:/HolonightChat/assets/providers/anthropic.svg",
        "google":    "qrc:/HolonightChat/assets/providers/google.svg",
    })
    readonly property var _entry: root._table[root.providerType]
    readonly property bool _known: root._entry !== undefined

    implicitWidth: 64
    implicitHeight: 64

    HnSurfaceFrame {
        id: frame

        anchors.fill: parent
        surfaceRole: HnSurfaceRole.Card
        fillColor: "transparent"
        borderColor: root.framed ? HoloniightPalette.borderSubtle : "transparent"
        borderWidth: root.framed ? HnMetrics.borderWidth : 0

        HnIcon {
            anchors.centerIn: parent
            visible: root._known
            source: root._known ? root._entry : ""
            rendering: HnIcon.Semantic
            normalColor: HoloniightPalette.textPrimary
            size: Math.round(Math.min(frame.width, frame.height) * 0.6)
        }

        Rectangle {
            anchors.centerIn: parent
            visible: !root._known
            width: parent.width * 0.5
            height: width
            radius: width / 2
            color: HoloniightPalette.accentBlue
        }
    }
}
