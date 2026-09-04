pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as QQC2
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

ProviderSettingsScaffold {
    id: root

    property var controller: UtilitySettingsController

    providerType: "utility"
    title: qsTr("Background AI")
    showProviderControls: false
    footerNoticeText: root.controller.saveNotice
    footerNoticeStatus: root.controller.saveNoticeStatus

    formContent: Component {
        ColumnLayout {
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 3

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    text: qsTr("Default utility model")
                    color: HoloniightPalette.textPrimary
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Used for background tasks such as automatic chat-title generation, "
                               + "unless a task-specific override is set below.")
                    color: HoloniightPalette.textMuted
                    wrapMode: Text.Wrap
                }
                ProviderModelPicker {
                    objectName: "defaultUtilityModelPicker"
                    Layout.fillWidth: true
                    providerInstances: root.controller.providerInstances
                    selectedProviderId: root.controller.defaultProviderId
                    selectedModelName: root.controller.defaultModelName
                    modelNames: root.controller.defaultModelNames
                    onProviderSelected: providerId => root.controller.defaultProviderId = providerId
                    onModelSelected: modelName => root.controller.defaultModelName = modelName
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

                Text {
                    text: qsTr("Chat titles")
                    color: HoloniightPalette.textPrimary
                    font.bold: true
                }
                Switch {
                    objectName: "chatTitleGenerationEnabledSwitch"
                    text: qsTr("Automatically generate conversation titles")
                    checked: root.controller.chatTitleGenerationEnabled
                    onToggled: root.controller.chatTitleGenerationEnabled = checked
                }
                Text {
                    text: qsTr("Model override for this feature")
                    color: HoloniightPalette.textPrimary
                    enabled: root.controller.chatTitleGenerationEnabled
                }
                ProviderModelPicker {
                    objectName: "titleOverrideModelPicker"
                    Layout.fillWidth: true
                    enabled: root.controller.chatTitleGenerationEnabled
                    providerInstances: root.controller.providerInstances
                    selectedProviderId: root.controller.titleOverrideProviderId
                    selectedModelName: root.controller.titleOverrideModelName
                    modelNames: root.controller.titleOverrideModelNames
                    onProviderSelected: providerId => root.controller.titleOverrideProviderId = providerId
                    onModelSelected: modelName => root.controller.titleOverrideModelName = modelName
                }
            }
        }
    }

    footerContent: Component {
        RowLayout {
            Button {
                objectName: "discardBackgroundAiButton"
                text: qsTr("Discard")
                enabled: root.controller.dirty
                onClicked: root.controller.discardDraft()
            }
            ProviderActionButton {
                objectName: "saveBackgroundAiButton"
                text: qsTr("Save changes")
                actionIconSource: "qrc:/HolonightChat/assets/icons/save.svg"
                highlighted: true
                enabled: root.controller.canSave
                onClicked: root.controller.save()
            }
        }
    }
}
