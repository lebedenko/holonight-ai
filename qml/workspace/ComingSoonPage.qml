import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Item {
    id: root

    property string sectionName: ""

    HnEmptyState {
        anchors.centerIn: parent
        titleText: root.sectionName
        descriptionText: qsTr("Coming soon")
    }
}
