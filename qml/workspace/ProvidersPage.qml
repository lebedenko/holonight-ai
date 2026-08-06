pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Loader {
    id: root

    property string selectedProviderId: ""
    property string selectedProviderType: ""
    sourceComponent: {
        if (root.selectedProviderId.length === 0) return emptyPanel
        switch (root.selectedProviderType) {
        case "ollama": return ollamaPanel
        case "openai": return openAiPanel
        case "anthropic": return anthropicPanel
        case "google": return googlePanel
        default: return unsupportedPanel
        }
    }

    Component {
        id: emptyPanel
        HnSurfaceFrame {
            objectName: "noProviderSelectedState"
            surfaceRole: HnSurfaceRole.Window

            Text {
                objectName: "noProviderSelectedText"
                anchors.centerIn: parent
                text: qsTr("Select a provider or add one to configure it")
                color: HoloniightPalette.textMuted
            }
        }
    }

    Component {
        id: ollamaPanel

        OllamaSettingsPanel {}
    }

    Component {
        id: openAiPanel

        OpenAISettingsPanel {}
    }

    Component {
        id: anthropicPanel

        AnthropicSettingsPanel {}
    }

    Component {
        id: googlePanel

        GoogleSettingsPanel {}
    }

    Component {
        id: unsupportedPanel

        UnsupportedProviderPanel {
            providerName: root.selectedProviderId
        }
    }
}
