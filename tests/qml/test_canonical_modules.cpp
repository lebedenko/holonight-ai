#include <QDir>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

namespace {

TEST(CanonicalQmlModules, ResolveCanonicalTypes) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));

  QQmlComponent component{&engine};
  component.setData(R"(
    import QtQuick
    import Holonight.Core as Core
    import Holonight.Controls as Controls

    Item {
      property bool canonicalPaletteResolved: Core.HoloniightPalette !== null
      property bool canonicalAppearanceResolved: Core.HnAppearance !== null

      Controls.HnSurfaceFrame {
        surfaceRole: Core.HnSurfaceRole.Panel

        Core.HnIcon {
          source: ""
        }
      }
    }
  )",
                    QUrl{QStringLiteral("inline:canonical-module-smoke.qml")});

  QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> object{component.create()};
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  EXPECT_TRUE(object->property("canonicalPaletteResolved").toBool());
  EXPECT_TRUE(object->property("canonicalAppearanceResolved").toBool());
}

TEST(CanonicalQmlModules, ConstructAdoptedSharedControls) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));

  QQmlComponent component{&engine};
  component.setData(R"(
    import QtQuick
    import Holonight.Controls as Controls

    Item {
      Controls.HnSearchField {}
      Controls.HnIconComboBox { model: ["One"] }
      Controls.HnTextArea {}
      Controls.HnNavigationDelegate { title: "Navigation" }
      Controls.HnListDelegate { title: "List" }
      Controls.HnActionDelegate { title: "Action" }
      Controls.HnStatusIndicator { text: "Ready" }
      Controls.HnEmptyState { titleText: "Empty" }
      Controls.HnPanelHeader { title: "Header" }
      Controls.HnHeaderBar { content: Item {} }
      Controls.HnSectionHeader { titleText: "Section" }
      Controls.HnFormField { labelText: "Field" }
      Controls.HnSettingsRow { titleText: "Setting" }
      Controls.HnActionBar {}
      Controls.HnSeparator { width: 20 }
    }
  )",
                    QUrl{QStringLiteral("inline:adopted-controls-smoke.qml")});

  QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> object{component.create()};
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
}

TEST(CanonicalQmlModules, InstalledModulesContainRuntimeArtifacts) {
  const QDir import_root{QStringLiteral(HOLONIGHT_QML_IMPORT_PATH)};

  for (const QString& module : {QStringLiteral("Holonight/Core"), QStringLiteral("Holonight/Controls")}) {
    const QDir module_dir{import_root.filePath(module)};
    ASSERT_TRUE(module_dir.exists()) << qPrintable(module_dir.path());
    EXPECT_TRUE(QFileInfo::exists(module_dir.filePath(QStringLiteral("qmldir"))));
    EXPECT_FALSE(module_dir.entryList({QStringLiteral("*.qmltypes")}, QDir::Files).isEmpty());
    EXPECT_FALSE(module_dir.entryList({QStringLiteral("*holonight*_qml.*")}, QDir::Files).isEmpty());
  }
}

}  // namespace
