pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

ProviderSettingsScaffold {
    id: root

    providerType: "ollama"
    title: qsTr("Ollama")
    headerStatus: Component {
        HnStatusIndicator {
            status: ProviderSettingsController.ollamaConnectionStatus === "connected" ? HnStatusIndicator.Success
                : (ProviderSettingsController.ollamaConnectionStatus === "error"
                    ? HnStatusIndicator.Error : HnStatusIndicator.Neutral)
            text: ProviderSettingsController.ollamaConnectionStatus === "connected" ? qsTr("Connected")
                : (ProviderSettingsController.ollamaConnectionStatus === "error"
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
                        labelText: qsTr("Server URL")
                        Controls.TextField {
                            Layout.fillWidth: true
                            text: ProviderSettingsController.baseUrl
                            placeholderText: qsTr("http://localhost:11434")
                            onTextChanged: ProviderSettingsController.baseUrl = text
                        }
                    }
                }
            }

            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Default model")
                        errorText: ProviderSettingsController.modelRefreshError
                        hasError: errorText.length > 0
                        HnIconComboBox {
                            Layout.fillWidth: true
                            model: ProviderSettingsController.availableModelNames
                            currentIndex: find(ProviderSettingsController.defaultModel)
                            displayText: currentIndex >= 0 ? textAt(currentIndex) : ProviderSettingsController.defaultModel
                            onActivated: index => ProviderSettingsController.defaultModel = textAt(index)
                        }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                        width: parent ? parent.width : implicitWidth
                        text: ProviderSettingsController.modelRefreshInProgress
                            ? qsTr("Refreshing models…") : qsTr("Refresh models")
                        actionIconSource: "qrc:/HolonightChat/assets/icons/refresh.svg"
                        enabled: !ProviderSettingsController.modelRefreshInProgress
                        onClicked: ProviderSettingsController.refreshModels()
                    }
                }
            }

            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
                        HnFormField {
                            Layout.fillWidth: true
                            labelText: qsTr("Context window")
                            Controls.SpinBox {
                                Layout.fillWidth: true
                                from: 128
                                to: 1000000
                                stepSize: 128
                                editable: true
                                value: ProviderSettingsController.contextWindow
                                onValueModified: ProviderSettingsController.contextWindow = value
                            }
                        }
                        HnFormField {
                            Layout.fillWidth: true
                            labelText: qsTr("Temperature")
                            RowLayout {
                                Controls.SpinBox {
                                    from: 0; to: 200; stepSize: 5; editable: true
                                    value: Math.round(ProviderSettingsController.temperature * 100)
                                    textFromValue: (value, locale) => Number(value / 100).toLocaleString(locale, "f", 2)
                                    valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 100)
                                    onValueModified: ProviderSettingsController.temperature = value / 100
                                }
                                Controls.Slider {
                                    Layout.fillWidth: true
                                    from: 0; to: 2; stepSize: 0.05
                                    value: ProviderSettingsController.temperature
                                    onMoved: ProviderSettingsController.temperature = value
                                }
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
                            checked: ProviderSettingsController.toolCallingEnabled
                            onToggled: ProviderSettingsController.toolCallingEnabled = checked
                        }
                    }
                }
            }

            ProviderFormActionRow {
                Layout.fillWidth: true
                fieldContent: Component {
                    HnFormField {
                        labelText: qsTr("Authentication (optional)")
                        helperText: !ProviderSettingsController.credentialStoreAvailable
                            ? qsTr("Secret storage unavailable") : ""
                        CredentialTextField {
                            Layout.fillWidth: true
                            enabled: ProviderSettingsController.credentialStoreAvailable
                                && !ProviderSettingsController.credentialOperationInProgress
                            actionsEnabled: enabled
                            text: ProviderSettingsController.authToken
                            placeholderText: ProviderSettingsController.hasStoredToken ? qsTr("(stored)") : qsTr("No token set")
                            onTextChanged: ProviderSettingsController.authToken = text
                            onClearRequested: ProviderSettingsController.authToken = ""
                        }
                    }
                }
                actionContent: Component {
                    ProviderActionButton {
                        width: parent ? parent.width : implicitWidth
                        text: ProviderSettingsController.testConnectionInProgress ? qsTr("Testing…") : qsTr("Test connection")
                        actionIconSource: "qrc:/HolonightChat/assets/icons/test-connection.svg"
                        icon.color: HoloniightPalette.primary
                        enabled: !ProviderSettingsController.testConnectionInProgress
                        onClicked: ProviderSettingsController.testConnection()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: ProviderSettingsController.testConnectionMessage
                    wrapMode: Text.Wrap
                    color: ProviderSettingsController.testConnectionStatus === "error" ? HoloniightPalette.error
                        : (ProviderSettingsController.testConnectionStatus === "success"
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
            onClicked: ProviderSettingsController.resetToDefaults()
        }
    }
    footerNoticeText: ProviderSettingsController.saveNotice
    footerNoticeStatus: ProviderSettingsController.saveNoticeStatus
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
                    && !ProviderSettingsController.credentialOperationInProgress
                onClicked: ProviderManagementController.save()
            }
        }
    }
}
