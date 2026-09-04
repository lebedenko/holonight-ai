pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HolonightChat
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root

    property bool compact: false
    horizontalPadding: HnMetrics.horizontalPadding(HnControlSize.Normal)
    readonly property string statusText: {
        switch (ChatViewModel.selectedProviderStatus) {
        case ChatViewModel.Checking: return qsTr("Checking…")
        case ChatViewModel.LoadingModels: return qsTr("Loading models…")
        case ChatViewModel.Connected: return qsTr("Connected")
        case ChatViewModel.SetupRequired: return qsTr("Setup required")
        case ChatViewModel.Unavailable: return qsTr("Unavailable")
        case ChatViewModel.Error: return qsTr("Error")
        default: return qsTr("Idle")
        }
    }
    readonly property int statusRole: {
        switch (ChatViewModel.selectedProviderStatus) {
        case ChatViewModel.Connected: return HnStatusIndicator.Success
        case ChatViewModel.SetupRequired: return HnStatusIndicator.Warning
        case ChatViewModel.Unavailable:
        case ChatViewModel.Error: return HnStatusIndicator.Error
        case ChatViewModel.Checking:
        case ChatViewModel.LoadingModels: return HnStatusIndicator.Info
        default: return HnStatusIndicator.Neutral
        }
    }

    signal providerSettingsRequested(string providerId)
    signal collapseRequested()

    content: RowLayout {
        id: headerContent

        function syncProviderSelection(): void {
            providerCombo.currentIndex = providerCombo.selectedIndex()
        }

        function syncModelSelection(): void {
            modelCombo.currentIndex = modelCombo.model.indexOf(ChatViewModel.selectedModelName)
        }

        spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

        Rectangle {
            Layout.preferredWidth: 3
            Layout.preferredHeight: HnMetrics.controlHeight(HnControlSize.Normal) * 0.7
            color: HoloniightPalette.accentCyan
            radius: width / 2
        }

        HnIconComboBox {
            id: providerCombo

            function selectedIndex(): int {
                for (let index = 0; index < model.length; ++index) {
                    if (model[index].provider_id === ChatViewModel.selectedProviderId)
                        return index
                }
                return -1
            }

            Layout.preferredWidth: 140
            Layout.minimumWidth: 72
            model: ChatViewModel.availableProviders
            textRole: "display_name"
            enabled: model.length > 0
            displayText: currentIndex >= 0 ? textAt(currentIndex) : qsTr("No providers")
            Accessible.name: qsTr("Provider")
            onActivated: index => ChatViewModel.selectedProviderId = model[index].provider_id
        }

        Text {
            text: "·"
            textFormat: Text.PlainText
            color: HoloniightPalette.textMuted
            Accessible.ignored: true
        }

        HnIconComboBox {
            id: modelCombo

            Layout.preferredWidth: 150
            Layout.minimumWidth: 72
            model: ChatViewModel.availableModelNames
            enabled: model.length > 0
            displayText: currentIndex >= 0 ? textAt(currentIndex) : qsTr("No models")
            Accessible.name: qsTr("Model")
            onActivated: index => ChatViewModel.selectedModelName = model[index]
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        HnStatusIndicator {
            status: root.statusRole
            text: root.width >= 390 ? root.statusText : ""
            Accessible.name: qsTr("Provider connection status")
            Accessible.description: root.statusText
                + (ChatViewModel.providerStatusMessage.length > 0
                   ? qsTr(": %1").arg(ChatViewModel.providerStatusMessage) : "")
        }

        HnIconButton {
            visible: !root.compact
            enabled: ChatViewModel.selectedProviderId.length > 0
            icon.source: "qrc:/HolonightChat/assets/icons/provider-settings.svg"
            Accessible.name: qsTr("Open selected provider settings")
            onClicked: root.providerSettingsRequested(ChatViewModel.selectedProviderId)
        }

        HnIconButton {
            visible: !root.compact
            icon.source: "qrc:/HolonightChat/assets/icons/collapse.svg"
            Accessible.name: qsTr("Collapse to quick panel")
            onClicked: root.collapseRequested()
        }

        Connections {
            target: ChatViewModel

            function onAvailableProvidersChanged(): void {
                headerContent.syncProviderSelection()
            }

            function onSelectedProviderIdChanged(): void {
                headerContent.syncProviderSelection()
            }

            function onAvailableModelNamesChanged(): void {
                headerContent.syncModelSelection()
            }

            function onSelectedModelNameChanged(): void {
                headerContent.syncModelSelection()
            }
        }

        Component.onCompleted: {
            syncProviderSelection()
            syncModelSelection()
        }
    }
}
