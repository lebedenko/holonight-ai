pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

HnApplicationWindow {
    id: root

    width: 1100
    height: 720
    minimumWidth: 1100
    minimumHeight: 720
    visible: false
    title: qsTr("Settings")
    contentPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)
    property string requestedProviderId: ""
    property var providerController: ProviderManagementController
    property var utilityController: UtilitySettingsController
    property string selectedProviderId: root.providerController.selectedInstanceId
    property string currentSection: "providers"

    function openProvider(providerId: string): void {
        requestedProviderId = providerId
        if (providerId.length > 0) root.providerController.requestSelection(providerId)
        visible = true
        raise()
        requestActivate()
    }

    function selectSection(sectionId: string): void {
        if (root.currentSection === "background-ai" && sectionId !== root.currentSection
                && root.utilityController.dirty)
            root.utilityController.discardDraft()
        root.currentSection = sectionId
    }

    RowLayout {
        anchors.fill: parent
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        SettingsSidebar {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            chamferedCornersOverride: HnCornerMask.TopRight | HnCornerMask.BottomRight
            currentSection: root.currentSection
            onSectionSelected: sectionId => root.selectSection(sectionId)
        }

        ProviderListPanel {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            visible: root.currentSection === "providers"
            chamferedCornersOverride: HnCornerMask.TopRight | HnCornerMask.BottomRight
            selectedProviderId: root.selectedProviderId
            providerController: root.providerController
            onProviderSelected: providerId => root.providerController.requestSelection(providerId)
        }

        ProvidersPage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.currentSection === "providers"
            selectedProviderId: root.selectedProviderId
            selectedProviderType: root.providerController.selectedProviderType
        }

        BackgroundAiSettingsPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.currentSection === "background-ai"
            chamferedCornersOverride: HnCornerMask.TopRight | HnCornerMask.BottomRight
        }
    }

    onClosing: close => {
        if (root.providerController.dirty) {
            close.accepted = false
            root.providerController.requestClose()
        } else if (root.utilityController.dirty) {
            root.utilityController.discardDraft()
        }
    }

    Connections {
        target: root.providerController
        function onCloseApproved(): void { root.close() }
    }

    Controls.Dialog {
        objectName: "dirtyNavigationDialog"
        anchors.centerIn: parent
        modal: true
        visible: root.providerController.navigationPromptVisible
        padding: HnMetrics.horizontalPadding(HnControlSize.Normal) * 2
        standardButtons: Controls.Dialog.NoButton

        background: HnSurfaceFrame {
            surfaceRole: HnSurfaceRole.Popup
        }

        contentItem: ColumnLayout {
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

            Text {
                Layout.fillWidth: true
                text: qsTr("Save provider changes?")
                color: HoloniightPalette.textPrimary
                font.pointSize: HolonightTheme.titleFontSize
                font.bold: true
            }

            RowLayout {
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Controls.Button {
                    objectName: "cancelDirtyNavigationButton"
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    onClicked: root.providerController.cancelNavigation()
                }
                Controls.Button {
                    objectName: "discardDirtyNavigationButton"
                    Layout.fillWidth: true
                    text: qsTr("Discard")
                    onClicked: root.providerController.discardAndContinue()
                }
                Controls.Button {
                    objectName: "saveDirtyNavigationButton"
                    Layout.fillWidth: true
                    text: qsTr("Save")
                    highlighted: true
                    enabled: root.providerController.canSave
                    onClicked: root.providerController.saveAndContinue()
                }
            }
        }
    }
}
