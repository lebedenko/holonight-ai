pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls

Controls.Button {
    id: root

    required property url actionIconSource

    icon.source: root.actionIconSource
}
