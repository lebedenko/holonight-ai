#include <QDir>
#include <QMetaObject>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>
#include <memory>

namespace {

std::unique_ptr<QObject> createRegistry(QQmlEngine& engine) {
  const QString path =
      QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml/shared/ToolRendererRegistry.qml"));
  QQmlComponent component{&engine, QUrl::fromLocalFile(path)};
  if (!component.isReady()) {
    ADD_FAILURE() << qPrintable(component.errorString());
    return {};
  }
  return std::unique_ptr<QObject>(component.create());
}

QObject* componentFor(QObject* registry, const QString& key) {
  QVariant result;
  const bool ok =
      QMetaObject::invokeMethod(registry, "componentFor", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, key));
  EXPECT_TRUE(ok);
  EXPECT_TRUE(result.isValid());
  EXPECT_TRUE(result.canConvert<QObject*>());
  return qvariant_cast<QObject*>(result);
}

TEST(ToolRendererRegistryQml, KnownKeysResolveDedicatedComponentsAndUnknownFallsBackToGeneric) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(HOLONIGHT_QML_IMPORT_PATH));
  engine.addImportPath(QDir{QStringLiteral(PROJECT_SOURCE_DIR)}.filePath(QStringLiteral("qml")));

  auto registry = createRegistry(engine);
  ASSERT_NE(registry, nullptr);

  const QVariant keysVariant = registry->property("registeredKeys");
  ASSERT_TRUE(keysVariant.canConvert<QVariantList>());

  const QVariantList keys = keysVariant.toList();
  EXPECT_TRUE(keys.contains("generic"));
  EXPECT_TRUE(keys.contains("filesystem.list"));

  QObject* genericComponent = componentFor(registry.get(), QStringLiteral("generic"));
  ASSERT_NE(genericComponent, nullptr);
  QObject* listFilesComponent = componentFor(registry.get(), QStringLiteral("filesystem.list"));
  ASSERT_NE(listFilesComponent, nullptr);
  QObject* unknownComponent = componentFor(registry.get(), QStringLiteral("completely.unknown.renderer"));
  ASSERT_NE(unknownComponent, nullptr);

  EXPECT_NE(listFilesComponent, genericComponent);
  EXPECT_EQ(unknownComponent, genericComponent);

  // Contract guard: every registered key resolves to a concrete component.
  for (const QVariant& key : keys) {
    EXPECT_NE(componentFor(registry.get(), key.toString()), nullptr) << qPrintable(key.toString());
  }
}

}  // namespace
