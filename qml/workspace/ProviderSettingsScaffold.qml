pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC2
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight
import Holonight as H

HnSurfaceFrame {
    id: root

    required property string providerType
    required property string title
    property var providerController: ProviderManagementController
    property bool showProviderControls: true
    property string headerTitle: showProviderControls ? providerController.displayName : title
    property string description: qsTr("Connection, model, generation, and credential settings")
    property Component headerStatus
    property Component formContent
    property Component resetContent
    property string footerNoticeText
    property string footerNoticeStatus: "idle"
    property Component footerContent

    surfaceRole: HnSurfaceRole.Window

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 3
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Provider instance")

            Text {
                Layout.fillWidth: true
                text: root.headerTitle
                color: HoloniightPalette.textPrimary
                font.pointSize: HolonightTheme.titleFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            Loader {
                sourceComponent: root.headerStatus
            }

            Switch {
                objectName: "providerEnabledCheckBox"
                text: qsTr("Enabled")
                visible: root.showProviderControls
                checked: root.showProviderControls ? root.providerController.enabled : false
                onToggled: root.providerController.enabled = checked
            }
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            Layout.rightMargin: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            HnSurfaceFrame {
                width: scrollView.availableWidth
                implicitHeight: formContentLayout.implicitHeight + HnMetrics.horizontalPadding(HnControlSize.Normal) * 4
                surfaceRole: HnSurfaceRole.Card

                ColumnLayout {
                    id: formContentLayout
                    anchors.fill: parent
                    anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
                    spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

                    HnFormField {
                        objectName: "providerNameField"
                        visible: root.showProviderControls
                        Layout.fillWidth: true
                        labelText: qsTr("Instance name")
                        errorText: root.providerController.nameValidationError
                        hasError: errorText.length > 0
                        TextField {
                            objectName: "providerNameEditor"
                            Layout.fillWidth: true
                            text: root.showProviderControls ? root.providerController.displayName : ""
                            placeholderText: root.title
                            onTextEdited: root.providerController.displayName = text
                        }
                    }

                    Text {
                        objectName: "deletionExplanationText"
                        Layout.fillWidth: true
                        visible: root.showProviderControls && root.providerController.deletionExplanation.length > 0
                        text: root.showProviderControls ? root.providerController.deletionExplanation : ""
                        color: HoloniightPalette.textMuted
                        wrapMode: Text.Wrap
                    }

                    Loader {
                        id: formLoader
                        Layout.fillWidth: true
                        sourceComponent: root.formContent
                    }
                }
            }
        }

        HnActionBar {
            Layout.fillWidth: true
            Layout.margins: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
            leadingContent: Component {
                RowLayout {
                    Loader {
                        sourceComponent: root.resetContent
                    }
                    Button {
                        objectName: "deleteProviderButton"
                        text: qsTr("Delete provider")
                        visible: root.showProviderControls
                        enabled: root.showProviderControls && root.providerController.canDelete
                        onClicked: deleteDialog.open()
                    }
                }
            }
            centerContent: Component {
                Text {
                    text: root.footerNoticeText
                    color: root.footerNoticeStatus === "success" ? HoloniightPalette.success
                        : (root.footerNoticeStatus === "warning" ? HoloniightPalette.warning
                            : (root.footerNoticeStatus === "error" ? HoloniightPalette.error
                                : HoloniightPalette.textMuted))
                    elide: Text.ElideRight
                }
            }
            trailingContent: root.footerContent
        }
    }

    QQC2.Dialog {
        id: deleteDialog
        objectName: "deleteProviderDialog"
        anchors.centerIn: parent
        modal: true
        padding: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
        standardButtons: QQC2.Dialog.NoButton
        onAccepted: root.providerController.deleteSelected(true)

        background: HnSurfaceFrame {
            surfaceRole: HnSurfaceRole.Popup
        }

        contentItem: ColumnLayout {
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

            Text {
                Layout.fillWidth: true
                text: qsTr("Delete provider?")
                color: HoloniightPalette.textPrimary
                font.pointSize: HolonightTheme.titleFontSize
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Existing messages keep this provider's historical name and icon.")
                color: HoloniightPalette.textMuted
                wrapMode: Text.Wrap
            }

            RowLayout {
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                H.Button {
                    objectName: "confirmDeleteProviderButton"
                    Layout.fillWidth: true
                    text: qsTr("Yes")
                    highlighted: true
                    onClicked: deleteDialog.accept()
                }

                H.Button {
                    objectName: "cancelDeleteProviderButton"
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: deleteDialog.reject()
                }
            }
        }
    }
}
