pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

ProviderSettingsScaffold {
    id: root
    providerType: "anthropic"
    title: qsTr("Anthropic")
    headerStatus: Component {
        HnStatusIndicator {
            status: AnthropicProviderSettingsController.anthropicConnectionStatus === "connected" ? HnStatusIndicator.Success
                : (AnthropicProviderSettingsController.anthropicConnectionStatus === "error"
                    ? HnStatusIndicator.Error : HnStatusIndicator.Neutral)
            text: AnthropicProviderSettingsController.anthropicConnectionStatus === "connected" ? qsTr("Connected")
                : (AnthropicProviderSettingsController.anthropicConnectionStatus === "error"
                    ? qsTr("Connection error") : qsTr("Not tested"))
        }
    }
    formContent: Component {
        ColumnLayout {
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Base URL")
                        Controls.TextField {
                            Layout.fillWidth: true; text: AnthropicProviderSettingsController.baseUrl
                            placeholderText: qsTr("https://api.anthropic.com")
                            onTextChanged: AnthropicProviderSettingsController.baseUrl = text
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Default model")
                        errorText: AnthropicProviderSettingsController.modelRefreshError
                        hasError: errorText.length > 0
                    HnIconComboBox {
                        Layout.fillWidth: true; model: AnthropicProviderSettingsController.availableModelNames
                        currentIndex: find(AnthropicProviderSettingsController.defaultModel)
                        displayText: currentIndex >= 0 ? textAt(currentIndex) : AnthropicProviderSettingsController.defaultModel
                        onActivated: index => AnthropicProviderSettingsController.defaultModel = textAt(index)
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                        width: parent ? parent.width : implicitWidth
                        text: AnthropicProviderSettingsController.modelRefreshInProgress
                            ? qsTr("Refreshing models…") : qsTr("Refresh models")
                        actionIconSource: "qrc:/HolonightChat/assets/icons/refresh.svg"
                        enabled: !AnthropicProviderSettingsController.modelRefreshInProgress
                        onClicked: AnthropicProviderSettingsController.refreshModels()
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    RowLayout {
                        spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
                        HnFormField {
                            Layout.fillWidth: true; labelText: qsTr("Temperature")
                            RowLayout {
                                Controls.SpinBox {
                                    from: 0; to: 100; stepSize: 5; editable: true
                                    value: Math.round(AnthropicProviderSettingsController.temperature * 100)
                                    textFromValue: (value, locale) => Number(value / 100).toLocaleString(locale, "f", 2)
                                    valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 100)
                                    onValueModified: AnthropicProviderSettingsController.temperature = value / 100
                                }
                                Controls.Slider {
                                    Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.05
                                    value: AnthropicProviderSettingsController.temperature
                                    onMoved: AnthropicProviderSettingsController.temperature = value
                                }
                            }
                        }
                        HnFormField {
                            Layout.fillWidth: true; labelText: qsTr("Max output tokens")
                            Controls.SpinBox {
                                Layout.fillWidth: true; from: 1; to: 200000; stepSize: 256; editable: true
                                value: AnthropicProviderSettingsController.maxOutputTokens
                                onValueModified: AnthropicProviderSettingsController.maxOutputTokens = value
                            }
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Enable tool calling")
                        helperText: qsTr("Lets the model list files under your home directory.")
                        Controls.Switch {
                            checked: AnthropicProviderSettingsController.toolCallingEnabled
                            onToggled: AnthropicProviderSettingsController.toolCallingEnabled = checked
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("API key")
                        helperText: !AnthropicProviderSettingsController.credentialStoreAvailable
                            ? qsTr("Secret storage unavailable") : ""
                    CredentialTextField {
                        Layout.fillWidth: true
                        enabled: AnthropicProviderSettingsController.credentialStoreAvailable
                            && !AnthropicProviderSettingsController.credentialOperationInProgress
                        actionsEnabled: enabled
                        text: AnthropicProviderSettingsController.authToken
                        placeholderText: AnthropicProviderSettingsController.hasStoredToken ? qsTr("(stored)") : qsTr("No API key set")
                        onTextChanged: AnthropicProviderSettingsController.authToken = text
                        onClearRequested: AnthropicProviderSettingsController.authToken = ""
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                    width: parent ? parent.width : implicitWidth
                    text: AnthropicProviderSettingsController.testConnectionInProgress ? qsTr("Testing…") : qsTr("Test connection")
                    actionIconSource: "qrc:/HolonightChat/assets/icons/test-connection.svg"
                    icon.color: HoloniightPalette.primary
                    enabled: !AnthropicProviderSettingsController.testConnectionInProgress
                    onClicked: AnthropicProviderSettingsController.testConnection()
                }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true; text: AnthropicProviderSettingsController.testConnectionMessage; wrapMode: Text.Wrap
                    color: AnthropicProviderSettingsController.testConnectionStatus === "error" ? HoloniightPalette.error
                        : (AnthropicProviderSettingsController.testConnectionStatus === "success"
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
            onClicked: AnthropicProviderSettingsController.resetToDefaults()
        }
    }
    footerNoticeText: AnthropicProviderSettingsController.saveNotice
    footerNoticeStatus: AnthropicProviderSettingsController.saveNoticeStatus
    footerContent: Component {
        RowLayout {
            Controls.Button {
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
                    && !AnthropicProviderSettingsController.credentialOperationInProgress
                onClicked: ProviderManagementController.save()
            }
        }
    }
}
