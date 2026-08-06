#include "holonight_platform/panel_surface.h"

// NOLINTBEGIN
#include "wayland-wlr-layer-shell-unstable-v1-client-protocol.h"
// NOLINTEND

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>
#include <QUrl>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow_p.h>
#include <qscreen_platform.h>

#include <algorithm>
#include <cstring>
#include <wayland-client.h>

Q_LOGGING_CATEGORY(lcPanelSurface, "holonight.chat.panel")

namespace holonight_platform {

namespace {

constexpr int kMinimumPanelWidth = 440;
constexpr int kMaximumPanelWidth = 560;
constexpr int kPanelMargin = 12;

}  // namespace

int preferredPanelWidth(int output_width) {
  const int scaled_width = static_cast<int>(static_cast<double>(output_width) * 0.30);
  return std::clamp(scaled_width, kMinimumPanelWidth, kMaximumPanelWidth);
}

PanelSurface::PanelSurface(QQmlEngine& engine, QObject* parent) : QObject(parent), engine_(engine) {}

PanelSurface::~PanelSurface() {
  destroySurface();
  if (layer_shell_ != nullptr) {
    // Wayland protocol handles are intentionally opaque aliases of wl_proxy.
    auto* proxy = reinterpret_cast<wl_proxy*>(layer_shell_);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (wl_proxy_get_version(proxy) >= 3) {
      zwlr_layer_shell_v1_destroy(layer_shell_);
    } else {
      wl_proxy_destroy(proxy);
    }
  }
  if (registry_ != nullptr) {
    wl_registry_destroy(registry_);
  }
}

bool PanelSurface::open(const QString& output_name) {
  if (view_ != nullptr) {
    return true;
  }

  QScreen* screen = findScreen(output_name);
  if (screen == nullptr) {
    const QString reason = QStringLiteral("Unknown output: %1").arg(output_name);
    qCWarning(lcPanelSurface) << reason;
    Q_EMIT failed(reason);
    return false;
  }

  if (!bindLayerShell()) {
    const QString reason = QStringLiteral("wlr-layer-shell is not available");
    qCWarning(lcPanelSurface) << reason;
    Q_EMIT failed(reason);
    return false;
  }

  auto* view = new QQuickView(&engine_, nullptr);
  view->setScreen(screen);
  view->setResizeMode(QQuickView::SizeRootObjectToView);
  view->setFlags(view->flags() | Qt::BypassWindowManagerHint);
  view->setColor(Qt::transparent);
  view->create();

  auto* wayland_window = view->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
  if (wayland_window == nullptr || wayland_window->surface() == nullptr) {
    const QString reason = QStringLiteral("Could not obtain a Wayland surface for the quick panel");
    qCWarning(lcPanelSurface) << reason;
    delete view;
    Q_EMIT failed(reason);
    return false;
  }

  wl_output* output = nullptr;
  if (auto* wayland_screen = screen->nativeInterface<QNativeInterface::QWaylandScreen>()) {
    output = wayland_screen->output();
  }

  view_ = view;
  wl_surface_ = wayland_window->surface();
  output_name_ = screen->name();
  configured_ = false;
  const QByteArray panel_namespace{"holonight-ai-panel"};
  layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(layer_shell_, wl_surface_, output,
                                                         ZWLR_LAYER_SHELL_V1_LAYER_TOP, panel_namespace.constData());
  if (layer_surface_ == nullptr) {
    const QString reason = QStringLiteral("Could not assign the quick-panel layer-surface role");
    qCWarning(lcPanelSurface) << reason;
    destroySurface();
    Q_EMIT failed(reason);
    return false;
  }

  static const zwlr_layer_surface_v1_listener listener{
      .configure = [](void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t width,
                      uint32_t height) { static_cast<PanelSurface*>(data)->handleConfigure(serial, width, height); },
      .closed = [](void* data, zwlr_layer_surface_v1*) { static_cast<PanelSurface*>(data)->handleClosed(); },
  };
  zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

  const uint32_t anchors =
      0U | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  zwlr_layer_surface_v1_set_anchor(layer_surface_, anchors);
  zwlr_layer_surface_v1_set_size(layer_surface_, static_cast<uint32_t>(preferredPanelWidth(screen->geometry().width())),
                                 0);
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, 0);
  zwlr_layer_surface_v1_set_margin(layer_surface_, kPanelMargin, 0, kPanelMargin, kPanelMargin);
  zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
                                                   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);

  view_->setInitialProperties({{QStringLiteral("outputName"), output_name_}});
  view_->setSource(QUrl{QStringLiteral("qrc:/HolonightChat/quickpanel/QuickPanel.qml")});
  if (view_->status() == QQuickView::Error) {
    const QString reason = QStringLiteral("Could not load QuickPanel.qml");
    qCWarning(lcPanelSurface) << reason << view_->errors();
    destroySurface();
    Q_EMIT failed(reason);
    return false;
  }

  wl_surface_commit(wl_surface_);
  return true;
}

void PanelSurface::close() {
  if (view_ == nullptr) {
    return;
  }
  destroySurface();
  Q_EMIT closed();
}

bool PanelSurface::isOpen() const { return view_ != nullptr; }

bool PanelSurface::isConfigured() const { return configured_; }

QString PanelSurface::outputName() const { return output_name_; }

QScreen* PanelSurface::findScreen(const QString& output_name) {
  if (output_name.isEmpty()) {
    return QGuiApplication::primaryScreen();
  }
  for (QScreen* screen : QGuiApplication::screens()) {
    if (screen->name() == output_name) {
      return screen;
    }
  }
  return nullptr;
}

bool PanelSurface::bindLayerShell() {
  if (layer_shell_ != nullptr) {
    return true;
  }

  auto* wayland_application = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  if (wayland_application == nullptr || wayland_application->display() == nullptr) {
    return false;
  }

  registry_ = wl_display_get_registry(wayland_application->display());
  if (registry_ == nullptr) {
    return false;
  }

  static const wl_registry_listener listener{
      .global =
          [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
            auto* self = static_cast<PanelSurface*>(data);
            if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
              self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
                  wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4U)));
            }
          },
      .global_remove = [](void*, wl_registry*, uint32_t) {},
  };
  wl_registry_add_listener(registry_, &listener, this);
  wl_display_roundtrip(wayland_application->display());
  return layer_shell_ != nullptr;
}

void PanelSurface::destroySurface() {
  if (layer_surface_ != nullptr) {
    zwlr_layer_surface_v1_destroy(layer_surface_);
    layer_surface_ = nullptr;
  }
  wl_surface_ = nullptr;
  configured_ = false;
  output_name_.clear();
  if (view_ != nullptr) {
    view_->hide();
    view_->deleteLater();
    view_ = nullptr;
  }
}

void PanelSurface::handleConfigure(uint32_t serial, uint32_t width, uint32_t height) {
  if (layer_surface_ == nullptr || view_ == nullptr) {
    return;
  }
  zwlr_layer_surface_v1_ack_configure(layer_surface_, serial);
  if (width > 0 && height > 0) {
    view_->resize(static_cast<int>(width), static_cast<int>(height));
  }
  if (!configured_) {
    configured_ = true;
    view_->show();
    Q_EMIT opened(output_name_);
  }
}

void PanelSurface::handleClosed() {
  destroySurface();
  Q_EMIT closed();
}

}  // namespace holonight_platform
