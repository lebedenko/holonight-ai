#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  QTemporaryDir isolation;
  if (!isolation.isValid()) {
    return 1;
  }
  for (const auto* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) {
    const auto path = isolation.path() + QLatin1Char('/') + QString::fromLatin1(name);
    if (!QDir{}.mkpath(path)) {
      return 1;
    }
    qputenv(name, path.toUtf8());
  }
  qputenv("XDG_RUNTIME_DIR", isolation.path().toUtf8());
  qputenv("QT_QUICK_BACKEND", "software");
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
  QGuiApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
