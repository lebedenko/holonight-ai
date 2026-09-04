import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnSurfaceFrame {
    id: root

    readonly property bool hasSelectedModel: ChatViewModel.selectedProviderId && ChatViewModel.selectedModelName

    surfaceRole: HnSurfaceRole.Panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HnHeaderBar {
            Layout.fillWidth: true
            dividerInset: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            content: HnPanelHeader {
                title: qsTr("Workspace")
                dividerVisible: false
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Active model")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    text: root.hasSelectedModel
                        ? ChatViewModel.selectedProviderId + "/" + ChatViewModel.selectedModelName
                        : qsTr("No model selected")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textPrimary
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Context")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("holonight-ai")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textPrimary
                    elide: Text.ElideRight
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Attachments")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("No attachments")
                    textFormat: Text.PlainText
                    color: HoloniightPalette.textMuted
                    elide: Text.ElideRight
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
