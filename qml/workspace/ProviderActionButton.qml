pragma ComponentBehavior: Bound

import QtQuick
import Holonight as H

H.Button {
    id: root

    required property url actionIconSource

    icon.source: root.actionIconSource
}
