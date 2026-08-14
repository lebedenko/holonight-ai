#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <holonight/wayland/layersurfacehost.h>
#include <holonight/wayland/layersurfacespec.h>
#include <memory>

class QQmlEngine;
class QScreen;

namespace holonight_platform {

[[nodiscard]] int preferredPanelWidth(int output_width);
[[nodiscard]] Holonight::Wayland::LayerSurfaceSpec buildPanelSurfaceSpec(QScreen& screen, QQmlEngine& engine);

namespace detail {

class PanelSurfaceHost {
 public:
  using ConfiguredHandler = std::function<void()>;
  using ClosedHandler = std::function<void()>;
  using FailedHandler = std::function<void(const QString&)>;

  virtual ~PanelSurfaceHost() = default;
  PanelSurfaceHost() = default;
  PanelSurfaceHost(const PanelSurfaceHost&) = delete;
  PanelSurfaceHost& operator=(const PanelSurfaceHost&) = delete;
  PanelSurfaceHost(PanelSurfaceHost&&) = delete;
  PanelSurfaceHost& operator=(PanelSurfaceHost&&) = delete;
  virtual bool open(const Holonight::Wayland::LayerSurfaceSpec& spec) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual QString diagnostic() const = 0;
  virtual void setHandlers(ConfiguredHandler configured, ClosedHandler closed, FailedHandler failed) = 0;
};

using PanelSurfaceHostFactory = std::function<std::unique_ptr<PanelSurfaceHost>()>;

}  // namespace detail

class PanelSurfaceTestAccess;

class PanelSurface : public QObject {
  Q_OBJECT

 public:
  explicit PanelSurface(QQmlEngine& engine, QObject* parent = nullptr);
  ~PanelSurface() override;

  PanelSurface(const PanelSurface&) = delete;
  PanelSurface& operator=(const PanelSurface&) = delete;
  PanelSurface(PanelSurface&&) = delete;
  PanelSurface& operator=(PanelSurface&&) = delete;

  [[nodiscard]] bool open(const QString& output_name = {});
  void close();
  [[nodiscard]] bool isOpen() const;
  [[nodiscard]] bool isConfigured() const;
  [[nodiscard]] QString outputName() const;

 Q_SIGNALS:
  void opened(const QString& output_name);
  void closed();
  void failed(const QString& reason);

 private:
  friend class PanelSurfaceTestAccess;
  PanelSurface(QQmlEngine& engine, detail::PanelSurfaceHostFactory host_factory, QObject* parent = nullptr);

  static QScreen* findScreen(const QString& output_name);
  void handleConfigured(const std::shared_ptr<const void>& identity);
  void queueClosed(const std::shared_ptr<const void>& identity);
  void queueFailed(const std::shared_ptr<const void>& identity, const QString& reason);
  void clearHost();

  QQmlEngine& engine_;
  detail::PanelSurfaceHostFactory host_factory_;
  std::unique_ptr<detail::PanelSurfaceHost> host_;
  std::shared_ptr<const void> host_identity_;
  QString output_name_;
  bool configured_{false};
};

}  // namespace holonight_platform
