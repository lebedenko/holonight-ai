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
    providerType: "google"
    title: qsTr("Google")
    headerStatus: Component {
        HnStatusIndicator {
            status: GoogleProviderSettingsController.googleConnectionStatus === "connected" ? HnStatusIndicator.Success
                : (GoogleProviderSettingsController.googleConnectionStatus === "error"
                    ? HnStatusIndicator.Error : HnStatusIndicator.Neutral)
            text: GoogleProviderSettingsController.googleConnectionStatus === "connected" ? qsTr("Connected")
                : (GoogleProviderSettingsController.googleConnectionStatus === "error"
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
                        labelText: qsTr("Base URL")
                        TextField {
                            Layout.fillWidth: true; text: GoogleProviderSettingsController.baseUrl
                            placeholderText: qsTr("https://generativelanguage.googleapis.com")
                            onTextChanged: GoogleProviderSettingsController.baseUrl = text
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Default model")
                        errorText: GoogleProviderSettingsController.modelRefreshError
                        hasError: errorText.length > 0
                    HnIconComboBox {
                        Layout.fillWidth: true; model: GoogleProviderSettingsController.availableModelNames
                        currentIndex: find(GoogleProviderSettingsController.defaultModel)
                        displayText: currentIndex >= 0 ? textAt(currentIndex) : GoogleProviderSettingsController.defaultModel
                        onActivated: index => GoogleProviderSettingsController.defaultModel = textAt(index)
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                        width: parent ? parent.width : implicitWidth
                        text: GoogleProviderSettingsController.modelRefreshInProgress
                            ? qsTr("Refreshing models…") : qsTr("Refresh models")
                        actionIconSource: "qrc:/HolonightChat/assets/icons/refresh.svg"
                        enabled: !GoogleProviderSettingsController.modelRefreshInProgress
                        onClicked: GoogleProviderSettingsController.refreshModels()
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    RowLayout {
                        spacing: HoloniightPalette.controlPadding * 2
                        HnFormField {
                            Layout.fillWidth: true; labelText: qsTr("Temperature")
                            RowLayout {
                                SpinBox {
                                    from: 0; to: 200; stepSize: 5; editable: true
                                    value: Math.round(GoogleProviderSettingsController.temperature * 100)
                                    textFromValue: (value, locale) => Number(value / 100).toLocaleString(locale, "f", 2)
                                    valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 100)
                                    onValueModified: GoogleProviderSettingsController.temperature = value / 100
                                }
                                Slider {
                                    Layout.fillWidth: true; from: 0; to: 2; stepSize: 0.05
                                    value: GoogleProviderSettingsController.temperature
                                    onMoved: GoogleProviderSettingsController.temperature = value
                                }
                            }
                        }
                        HnFormField {
                            Layout.fillWidth: true; labelText: qsTr("Max output tokens")
                            SpinBox {
                                Layout.fillWidth: true; from: 1; to: 200000; stepSize: 256; editable: true
                                value: GoogleProviderSettingsController.maxOutputTokens
                                onValueModified: GoogleProviderSettingsController.maxOutputTokens = value
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
                        Switch {
                            checked: GoogleProviderSettingsController.toolCallingEnabled
                            onToggled: GoogleProviderSettingsController.toolCallingEnabled = checked
                        }
                    }
                }
            }
            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("API key")
                        helperText: !GoogleProviderSettingsController.credentialStoreAvailable
                            ? qsTr("Secret storage unavailable") : ""
                    CredentialTextField {
                        Layout.fillWidth: true
                        enabled: GoogleProviderSettingsController.credentialStoreAvailable
                            && !GoogleProviderSettingsController.credentialOperationInProgress
                        actionsEnabled: enabled
                        text: GoogleProviderSettingsController.authToken
                        placeholderText: GoogleProviderSettingsController.hasStoredToken ? qsTr("(stored)") : qsTr("No API key set")
                        onTextChanged: GoogleProviderSettingsController.authToken = text
                        onClearRequested: GoogleProviderSettingsController.authToken = ""
                    }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                    width: parent ? parent.width : implicitWidth
                    text: GoogleProviderSettingsController.testConnectionInProgress ? qsTr("Testing…") : qsTr("Test connection")
                    actionIconSource: "qrc:/HolonightChat/assets/icons/test-connection.svg"
                    icon.color: HoloniightPalette.primary
                    enabled: !GoogleProviderSettingsController.testConnectionInProgress
                    onClicked: GoogleProviderSettingsController.testConnection()
                }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true; text: GoogleProviderSettingsController.testConnectionMessage; wrapMode: Text.Wrap
                    color: GoogleProviderSettingsController.testConnectionStatus === "error" ? HoloniightPalette.error
                        : (GoogleProviderSettingsController.testConnectionStatus === "success"
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
            onClicked: GoogleProviderSettingsController.resetToDefaults()
        }
    }
    footerNoticeText: GoogleProviderSettingsController.saveNotice
    footerNoticeStatus: GoogleProviderSettingsController.saveNoticeStatus
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
                    && !GoogleProviderSettingsController.credentialOperationInProgress
                onClicked: ProviderManagementController.save()
            }
        }
    }
}
