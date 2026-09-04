pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

ColumnLayout {
    id: root

    property var providerInstances: []
    property string selectedProviderId: ""
    property string selectedModelName: ""
    property var modelNames: []

    signal providerSelected(string providerId)
    signal modelSelected(string modelName)

    spacing: HnMetrics.internalSpacing(HnControlSize.Normal) * 2

    function providerIndex(): int {
        for (let index = 0; index < root.providerInstances.length; ++index) {
            if (root.providerInstances[index].provider_id === root.selectedProviderId)
                return index
        }
        return -1
    }

    HnFormField {
        objectName: "providerInstanceField"
        Layout.fillWidth: true
        labelText: qsTr("Provider instance")
        HnIconComboBox {
            id: providerCombo
            objectName: "providerInstanceCombo"
            Layout.fillWidth: true
            model: root.providerInstances
            textRole: "display_name"
            currentIndex: root.providerIndex()
            onActivated: index => root.providerSelected(model[index].provider_id)
        }
    }

    HnFormField {
        objectName: "modelField"
        Layout.fillWidth: true
        visible: root.selectedProviderId.length > 0
        labelText: qsTr("Model")
        HnIconComboBox {
            id: modelCombo
            objectName: "modelCombo"
            Layout.fillWidth: true
            model: root.modelNames
            currentIndex: find(root.selectedModelName)
            displayText: currentIndex >= 0 ? textAt(currentIndex) : root.selectedModelName
            onActivated: index => root.modelSelected(textAt(index))
        }
    }
}
