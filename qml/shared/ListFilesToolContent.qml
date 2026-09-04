import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls
import Holonight as H

Item {
    id: root

    property var toolCall: ({})

    readonly property var detailData: root.toolCall && root.toolCall.detailData
                                      ? root.toolCall.detailData
                                      : ({})
    readonly property var entries: root.detailData.entries || []
    readonly property int previewLimit: 20
    readonly property int visibleEntryCount: root.showAllEntries
                                             ? root.entries.length
                                             : Math.min(root.previewLimit, root.entries.length)
    readonly property string pathText: root.detailData.path || qsTr("(current)")
    readonly property string errorMessage: root.detailData.errorMessage || ""
    readonly property bool hasError: root.errorMessage.length > 0 || root.toolCall.isError === true
    property bool showAllEntries: false

    implicitWidth: Math.max(280, column.implicitWidth + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2)
    implicitHeight: column.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Text {
            objectName: "listFilesPath"
            text: qsTr("Path: %1").arg(root.pathText)
            textFormat: Text.PlainText
            Layout.fillWidth: true
            color: HoloniightPalette.textPrimary
            font.bold: true
            elide: Text.ElideMiddle
        }

        Text {
            objectName: "listFilesMetadata"
            text: qsTr("%1 · %2 files · %3 folders")
                  .arg(root.toolCall.status || qsTr("Unknown"))
                  .arg(Number(root.detailData.fileCount) || 0)
                  .arg(Number(root.detailData.directoryCount) || 0)
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            Layout.fillWidth: true
        }

        Rectangle {
            objectName: "listFilesErrorState"
            visible: root.hasError
            Layout.fillWidth: true
            implicitHeight: errorText.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            radius: height / 2
            color: HoloniightPalette.surfaceElevated
            border.color: HoloniightPalette.borderUrgent

            Text {
                id: errorText

                anchors.fill: parent
                anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)
                text: root.errorMessage.length > 0 ? root.errorMessage : qsTr("The directory could not be listed.")
                textFormat: Text.PlainText
                color: HoloniightPalette.error
                wrapMode: Text.Wrap
            }
        }

        Text {
            objectName: "listFilesEmptyState"
            visible: !root.hasError && root.entries.length === 0
            text: qsTr("This directory is empty.")
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            font.italic: true
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        ListView {
            id: entryList

            objectName: "listFilesEntryList"
            visible: !root.hasError && root.entries.length > 0
            Layout.fillWidth: true
            implicitHeight: Math.min(contentHeight, 260)
            clip: true
            spacing: 2
            model: root.entries.slice(0, root.visibleEntryCount)

            delegate: Item {
                required property var modelData

                width: entryList.width
                height: Math.max(24, entryName.implicitHeight + 4)

                RowLayout {
                    anchors.fill: parent
                    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                    HnIcon {
                        objectName: "listFilesEntryIcon"
                        source: modelData.kind === "directory"
                                ? "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
                                : "qrc:/HolonightChat/assets/icons/documentation.svg"
                        size: 16
                        iconState: HnIcon.Muted
                    }

                    Text {
                        id: entryName

                        objectName: "listFilesEntryName"
                        text: modelData.name || qsTr("(unnamed entry)")
                        textFormat: Text.PlainText
                        color: HoloniightPalette.textPrimary
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        H.Button {
            objectName: "listFilesViewAll"
            visible: !root.hasError && !root.showAllEntries && root.entries.length > root.previewLimit
            text: qsTr("View all (%1)").arg(root.entries.length)
            Accessible.name: qsTr("View all directory entries")
            onClicked: root.showAllEntries = true
        }
    }
}
