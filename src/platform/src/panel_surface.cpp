#include "holonight_platform/panel_surface.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QQmlEngine>
#include <QScreen>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcPanelSurface, "holonight.chat.panel")

namespace holonight_platform {

namespace {

constexpr int kMinimumPanelWidth = 440;
constexpr int kMaximumPanelWidth = 560;
constexpr int kPanelMargin = 12;

class SharedPanelSurfaceHost final : public detail::PanelSurfaceHost {
 public:
  SharedPanelSurfaceHost() {
    QObject::connect(&host_, &Holonight::Wayland::LayerSurfaceHost::configured, &host_,
                     [this]() { configured_handler_(); });
    QObject::connect(&host_, &Holonight::Wayland::LayerSurfaceHost::closed, &host_, [this]() { closed_handler_(); });
    QObject::connect(&host_, &Holonight::Wayland::LayerSurfaceHost::failed, &host_,
                     [this](const QString& reason) { failed_handler_(reason); });
  }

  bool open(const Holonight::Wayland::LayerSurfaceSpec& spec) override { return host_.open(spec); }
  void close() override { host_.close(); }
  [[nodiscard]] QString diagnostic() const override { return host_.diagnostic(); }
  void setHandlers(ConfiguredHandler configured, ClosedHandler closed, FailedHandler failed) override {
    configured_handler_ = std::move(configured);
    closed_handler_ = std::move(closed);
    failed_handler_ = std::move(failed);
  }

 private:
  Holonight::Wayland::LayerSurfaceHost host_;
  ConfiguredHandler configured_handler_ = [] {};
  ClosedHandler closed_handler_ = [] {};
  FailedHandler failed_handler_ = [](const QString&) {};
};

detail::PanelSurfaceHostFactory defaultHostFactory() {
  return [] { return std::make_unique<SharedPanelSurfaceHost>(); };
}

}  // namespace

int preferredPanelWidth(int output_width) {
  const int scaled_width = static_cast<int>(static_cast<double>(output_width) * 0.30);
  return std::clamp(scaled_width, kMinimumPanelWidth, kMaximumPanelWidth);
}

Holonight::Wayland::LayerSurfaceSpec buildPanelSurfaceSpec(QScreen& screen, QQmlEngine& engine) {
  using Holonight::Wayland::Anchor;
  using Holonight::Wayland::InputRegionPolicy;
  using Holonight::Wayland::KeyboardInteractivity;
  using Holonight::Wayland::Layer;

  Holonight::Wayland::LayerSurfaceSpec spec;
  spec.output = &screen;
  spec.name_space = QStringLiteral("holonight-ai-panel");
  spec.layer = Layer::Top;
  spec.anchors = Anchor::Top | Anchor::Bottom | Anchor::Left;
  spec.width = preferredPanelWidth(screen.geometry().width());
  spec.height = 0;
  spec.margin_top = kPanelMargin;
  spec.margin_right = 0;
  spec.margin_bottom = kPanelMargin;
  spec.margin_left = kPanelMargin;
  spec.exclusive_zone = 0;
  spec.keyboard_interactivity = KeyboardInteractivity::Exclusive;
  spec.input_region_policy = InputRegionPolicy::Default;
  spec.qml_url = QUrl{QStringLiteral("qrc:/HolonightChat/quickpanel/QuickPanel.qml")};
  spec.initial_properties = {{QStringLiteral("outputName"), screen.name()}};
  spec.window_flags = Qt::FramelessWindowHint | Qt::BypassWindowManagerHint;
  spec.color = Qt::transparent;
  spec.engine = &engine;
  return spec;
}

PanelSurface::PanelSurface(QQmlEngine& engine, QObject* parent) : PanelSurface(engine, defaultHostFactory(), parent) {}

PanelSurface::PanelSurface(QQmlEngine& engine, detail::PanelSurfaceHostFactory host_factory, QObject* parent)
    : QObject(parent), engine_(engine), host_factory_(std::move(host_factory)) {}

PanelSurface::~PanelSurface() = default;

bool PanelSurface::open(const QString& output_name) {
  if (host_ != nullptr) {
    return true;
  }

  QScreen* screen = findScreen(output_name);
  if (screen == nullptr) {
    const QString reason = QStringLiteral("Unknown output: %1").arg(output_name);
    qCWarning(lcPanelSurface) << reason;
    Q_EMIT failed(reason);
    return false;
  }

  auto host = host_factory_();
  auto identity = std::make_shared<char>();
  host->setHandlers([this, identity]() { handleConfigured(identity); }, [this, identity]() { queueClosed(identity); },
                    [this, identity](const QString& reason) { queueFailed(identity, reason); });
  output_name_ = screen->name();
  configured_ = false;
  host_ = std::move(host);
  host_identity_ = identity;
  if (!host_->open(buildPanelSurfaceSpec(*screen, engine_))) {
    const QString reason = host_->diagnostic();
    qCWarning(lcPanelSurface) << reason;
    clearHost();
    Q_EMIT failed(reason);
    return false;
  }
  return true;
}

void PanelSurface::close() {
  if (host_ == nullptr) {
    return;
  }
  auto host = std::move(host_);
  clearHost();
  host->close();
  Q_EMIT closed();
}

bool PanelSurface::isOpen() const { return host_ != nullptr; }
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

void PanelSurface::handleConfigured(const std::shared_ptr<const void>& identity) {
  if (host_identity_ != identity || configured_) {
    return;
  }
  configured_ = true;
  Q_EMIT opened(output_name_);
}

void PanelSurface::queueClosed(const std::shared_ptr<const void>& identity) {
  QMetaObject::invokeMethod(
      this,
      [this, identity]() {
        if (host_identity_ != identity) {
          return;
        }
        clearHost();
        Q_EMIT closed();
      },
      Qt::QueuedConnection);
}

void PanelSurface::queueFailed(const std::shared_ptr<const void>& identity, const QString& reason) {
  QMetaObject::invokeMethod(
      this,
      [this, identity, reason]() {
        if (host_identity_ != identity) {
          return;
        }
        clearHost();
        Q_EMIT failed(reason);
      },
      Qt::QueuedConnection);
}

void PanelSurface::clearHost() {
  host_.reset();
  host_identity_.reset();
  configured_ = false;
  output_name_.clear();
}

}  // namespace holonight_platform
