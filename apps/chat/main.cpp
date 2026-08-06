#include "ChatApplication.h"

#include <QGuiApplication>

int main(int argc, char* argv[]) {
  qputenv("QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT", "1");
  ChatApplication app(argc, argv);
  return QGuiApplication::exec();
}
