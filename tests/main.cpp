#include <QByteArray>
#include <QGuiApplication>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  qputenv("QML_IMPORT_PATH", QByteArray(HOLONIGHT_QML_IMPORT_PATH));
  QGuiApplication app(argc, argv);
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
