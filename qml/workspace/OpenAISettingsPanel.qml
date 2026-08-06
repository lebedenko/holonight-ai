pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls
import Holonight

ProviderSettingsScaffold {
    id: root
    providerType: "openai"
    title: qsTr("OpenAI")
    headerStatus: Component {
        HnStatusIndicator {
            status: OpenAIProviderSettingsController.openAiConnectionStatus === "connected" ? HnStatusIndicator.Success
                : (OpenAIProviderSettingsController.openAiConnectionStatus === "error"
                    ? HnStatusIndicator.Error : HnStatusIndicator.Neutral)
            text: OpenAIProviderSettingsController.openAiConnectionStatus === "connected" ? qsTr("Connected")
                : (OpenAIProviderSettingsController.openAiConnectionStatus === "error"
                    ? qsTr("Connection error") : qsTr("Not tested"))
        }
    }
    formContent: Component {
        ColumnLayout {
            spacing: HoloniightPalette.controlPadding * 2
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Enable tool calling")
                        helperText: qsTr("Lets the model list files under your home directory.")
                        Switch {
                            checked: OpenAIProviderSettingsController.toolCallingEnabled
                            onToggled: OpenAIProviderSettingsController.toolCallingEnabled = checked
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Base URL")
                        TextField {
                            Layout.fillWidth: true; text: OpenAIProviderSettingsController.baseUrl
                            placeholderText: qsTr("https://api.openai.com/v1")
                            onTextChanged: OpenAIProviderSettingsController.baseUrl = text
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Default model")
                        errorText: OpenAIProviderSettingsController.modelRefreshError
                        hasError: errorText.length > 0
                    HnIconComboBox {
                        Layout.fillWidth: true; model: OpenAIProviderSettingsController.availableModelNames
                        currentIndex: find(OpenAIProviderSettingsController.defaultModel)
                        displayText: currentIndex >= 0 ? textAt(currentIndex) : OpenAIProviderSettingsController.defaultModel
                        onActivated: index => OpenAIProviderSettingsController.defaultModel = textAt(index)
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                        width: parent ? parent.width : implicitWidth
                        text: OpenAIProviderSettingsController.modelRefreshInProgress
                            ? qsTr("Refreshing models…") : qsTr("Refresh models")
                        actionIconSource: "qrc:/HolonightChat/assets/icons/refresh.svg"
                        enabled: !OpenAIProviderSettingsController.modelRefreshInProgress
                        onClicked: OpenAIProviderSettingsController.refreshModels()
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Temperature")
                        RowLayout {
                            SpinBox {
                                from: 0; to: 200; stepSize: 5; editable: true
                                value: Math.round(OpenAIProviderSettingsController.temperature * 100)
                                textFromValue: (value, locale) => Number(value / 100).toLocaleString(locale, "f", 2)
                                valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 100)
                                onValueModified: OpenAIProviderSettingsController.temperature = value / 100
                            }
                            Slider {
                                Layout.fillWidth: true; from: 0; to: 2; stepSize: 0.05
                                value: OpenAIProviderSettingsController.temperature
                                onMoved: OpenAIProviderSettingsController.temperature = value
                            }
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("API key")
                        helperText: !OpenAIProviderSettingsController.credentialStoreAvailable
                            ? qsTr("Secret storage unavailable") : ""
                    CredentialTextField {
                        Layout.fillWidth: true
                        enabled: OpenAIProviderSettingsController.credentialStoreAvailable
                            && !OpenAIProviderSettingsController.credentialOperationInProgress
                        actionsEnabled: enabled
                        text: OpenAIProviderSettingsController.authToken
                        placeholderText: OpenAIProviderSettingsController.hasStoredToken ? qsTr("(stored)") : qsTr("No API key set")
                        onTextChanged: OpenAIProviderSettingsController.authToken = text
                        onClearRequested: OpenAIProviderSettingsController.authToken = ""
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                    width: parent ? parent.width : implicitWidth
                    text: OpenAIProviderSettingsController.testConnectionInProgress ? qsTr("Testing…") : qsTr("Test connection")
                    actionIconSource: "qrc:/HolonightChat/assets/icons/test-connection.svg"
                    icon.color: HoloniightPalette.primary
                    enabled: !OpenAIProviderSettingsController.testConnectionInProgress
                    onClicked: OpenAIProviderSettingsController.testConnection()
                }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true; text: OpenAIProviderSettingsController.testConnectionMessage; wrapMode: Text.Wrap
                    color: OpenAIProviderSettingsController.testConnectionStatus === "error" ? HoloniightPalette.error
                        : (OpenAIProviderSettingsController.testConnectionStatus === "success"
                            ? HoloniightPalette.success : HoloniightPalette.textMuted)
                }
            }
        }
    }
    resetContent: Component {
        ProviderActionButton {
            text: qsTr("Reset")
            actionIconSource: "qrc:/HolonightChat/assets/icons/reset.svg"
            icon.color: HoloniightPalette.error
            onClicked: OpenAIProviderSettingsController.resetToDefaults()
        }
    }
    footerNoticeText: OpenAIProviderSettingsController.saveNotice
    footerNoticeStatus: OpenAIProviderSettingsController.saveNoticeStatus
    footerContent: Component {
        RowLayout {
            Button {
                objectName: "discardProviderButton"
                text: ProviderManagementController.addition ? qsTr("Cancel") : qsTr("Discard")
                enabled: ProviderManagementController.canSave
                onClicked: ProviderManagementController.discardDraft()
            }
            ProviderActionButton {
                objectName: "saveProviderButton"
                text: qsTr("Save changes")
                actionIconSource: "qrc:/HolonightChat/assets/icons/save.svg"
                highlighted: true
                enabled: ProviderManagementController.canSave
                    && !OpenAIProviderSettingsController.credentialOperationInProgress
                onClicked: ProviderManagementController.save()
            }
        }
    }
}
