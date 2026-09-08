pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

HnSurfaceFrame {
    id: root

    property string selectedProviderId: ""
    property var providerController: ProviderManagementController
    signal providerSelected(string providerId)

    surfaceRole: HnSurfaceRole.Panel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal) * 2
        spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

        HnPanelHeader {
            Layout.fillWidth: true
            title: qsTr("Providers")
            description: qsTr("Connection settings")
            dividerVisible: false
        }

        Controls.Button {
            id: addButton
            objectName: "addProviderButton"
            Layout.fillWidth: true
            text: qsTr("Add provider")
            onClicked: addMenu.open()

            Controls.Menu {
                id: addMenu
                objectName: "addProviderMenu"
                y: addButton.height
                Controls.MenuItem {
                    objectName: "addOllamaProviderAction"
                    text: qsTr("Ollama")
                    onTriggered: root.providerController.addProvider("ollama")
                }
                Controls.MenuItem {
                    objectName: "addOpenAIProviderAction"
                    text: qsTr("OpenAI")
                    onTriggered: root.providerController.addProvider("openai")
                }
                Controls.MenuItem {
                    objectName: "addAnthropicProviderAction"
                    text: qsTr("Anthropic")
                    onTriggered: root.providerController.addProvider("anthropic")
                }
                Controls.MenuItem {
                    objectName: "addGoogleProviderAction"
                    text: qsTr("Google")
                    onTriggered: root.providerController.addProvider("google")
                }
            }
        }

        ListView {
            objectName: "providerInstanceList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.providerController.instances
            spacing: HnMetrics.internalSpacing(HnControlSize.Normal)
            clip: true
            currentIndex: -1

            delegate: ProviderListDelegate {
                id: delegate
                objectName: "providerDelegate_" + delegate.model.instanceId
                required property var model
                width: ListView.view.width
                providerId: delegate.model.instanceId
                providerType: delegate.model.providerType
                providerName: delegate.model.displayName
                providerEnabled: delegate.model.enabled
                isSelected: delegate.model.instanceId === root.selectedProviderId
                onClicked: root.providerSelected(delegate.model.instanceId)
            }
        }
    }
}
