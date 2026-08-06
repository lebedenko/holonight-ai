import QtQuick

QtObject {
  id: root

  readonly property Component genericRenderer: Component {
    GenericToolContent {}
  }

  readonly property Component listFilesRenderer: Component {
    ListFilesToolContent {}
  }

  readonly property var rendererByKey: ({
    "generic": genericRenderer,
    "filesystem.list": listFilesRenderer
  })

  function componentFor(rendererKey) {
    if (root.rendererByKey.hasOwnProperty(rendererKey)) {
      return root.rendererByKey[rendererKey];
    }
    return root.rendererByKey.generic;
  }

  readonly property var registeredKeys: Object.keys(root.rendererByKey)
}
