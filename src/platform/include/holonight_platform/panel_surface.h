#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QQmlEngine;
class QQuickView;
class QScreen;
struct wl_output;
struct wl_registry;
struct wl_surface;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace holonight_platform {

[[nodiscard]] int preferredPanelWidth(int output_width);

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
  static QScreen* findScreen(const QString& output_name);
  [[nodiscard]] bool bindLayerShell();
  void destroySurface();
  void handleConfigure(uint32_t serial, uint32_t width, uint32_t height);
  void handleClosed();

  QQmlEngine& engine_;
  QQuickView* view_{nullptr};
  wl_registry* registry_{nullptr};
  zwlr_layer_shell_v1* layer_shell_{nullptr};
  zwlr_layer_surface_v1* layer_surface_{nullptr};
  wl_surface* wl_surface_{nullptr};
  QString output_name_;
  bool configured_{false};
};

}  // namespace holonight_platform
