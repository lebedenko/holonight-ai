#include "holonight_platform/panel_surface.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QSignalSpy>

#include <deque>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

namespace holonight_platform {

class PanelSurfaceTestAccess {
 public:
  static std::unique_ptr<PanelSurface> create(QQmlEngine& engine, detail::PanelSurfaceHostFactory factory) {
    return std::unique_ptr<PanelSurface>(new PanelSurface(engine, std::move(factory)));
  }
};

}  // namespace holonight_platform

namespace {

using Holonight::Wayland::Anchor;
using Holonight::Wayland::InputRegionPolicy;
using Holonight::Wayland::KeyboardInteractivity;
using Holonight::Wayland::Layer;
using Holonight::Wayland::LayerSurfaceSpec;
using holonight_platform::PanelSurface;
using holonight_platform::detail::PanelSurfaceHost;

struct FakeHostState {
  bool open_result{true};
  QString diagnostic{QStringLiteral("host failure")};
  int open_calls{0};
  int close_calls{0};
  LayerSurfaceSpec spec;
  PanelSurfaceHost::ConfiguredHandler configured;
  PanelSurfaceHost::ClosedHandler closed;
  PanelSurfaceHost::FailedHandler failed;
};

class FakeHost final : public PanelSurfaceHost {
 public:
  explicit FakeHost(std::shared_ptr<FakeHostState> state) : state_(std::move(state)) {}

  bool open(const LayerSurfaceSpec& spec) override {
    ++state_->open_calls;
    state_->spec = spec;
    return state_->open_result;
  }
  void close() override {
    ++state_->close_calls;
    state_->closed();
  }
  [[nodiscard]] QString diagnostic() const override { return state_->diagnostic; }
  void setHandlers(ConfiguredHandler configured, ClosedHandler closed, FailedHandler failed) override {
    state_->configured = std::move(configured);
    state_->closed = std::move(closed);
    state_->failed = std::move(failed);
  }

 private:
  std::shared_ptr<FakeHostState> state_;
};

class HostQueue {
 public:
  std::shared_ptr<FakeHostState> add() {
    auto state = std::make_shared<FakeHostState>();
    states_.push_back(state);
    return state;
  }

  holonight_platform::detail::PanelSurfaceHostFactory factory() {
    return [this] {
      auto state = states_.front();
      states_.pop_front();
      return std::make_unique<FakeHost>(std::move(state));
    };
  }

 private:
  std::deque<std::shared_ptr<FakeHostState>> states_;
};

TEST(PanelSurfacePolicy, PreferredWidthClampsSmallOutputsToMinimum) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(1200), 440);
}

TEST(PanelSurfacePolicy, PreferredWidthScalesWithinBounds) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(1600), 480);
}

TEST(PanelSurfacePolicy, PreferredWidthClampsLargeOutputsToMaximum) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(2560), 560);
}

TEST(PanelSurfacePolicy, BuildsCompleteSharedHostSpec) {
  QQmlEngine engine;
  QScreen* screen = QGuiApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);

  const LayerSurfaceSpec spec = holonight_platform::buildPanelSurfaceSpec(*screen, engine);
  EXPECT_EQ(spec.output, screen);
  EXPECT_EQ(spec.engine, &engine);
  EXPECT_EQ(spec.name_space, QStringLiteral("holonight-ai-panel"));
  EXPECT_EQ(spec.layer, Layer::Top);
  EXPECT_EQ(spec.anchors, Anchor::Top | Anchor::Bottom | Anchor::Left);
  EXPECT_EQ(spec.width, holonight_platform::preferredPanelWidth(screen->geometry().width()));
  EXPECT_EQ(spec.height, 0);
  EXPECT_EQ(spec.margin_top, 12);
  EXPECT_EQ(spec.margin_right, 0);
  EXPECT_EQ(spec.margin_bottom, 12);
  EXPECT_EQ(spec.margin_left, 12);
  EXPECT_EQ(spec.exclusive_zone, 0);
  EXPECT_EQ(spec.keyboard_interactivity, KeyboardInteractivity::Exclusive);
  EXPECT_EQ(spec.input_region_policy, InputRegionPolicy::Default);
  EXPECT_TRUE(spec.input_region.isEmpty());
  EXPECT_EQ(spec.qml_url, QUrl(QStringLiteral("qrc:/HolonightChat/quickpanel/QuickPanel.qml")));
  EXPECT_EQ(spec.initial_properties.value(QStringLiteral("outputName")).toString(), screen->name());
  EXPECT_EQ(spec.window_flags, Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
  EXPECT_EQ(spec.color, QColor(Qt::transparent));
  EXPECT_FALSE(static_cast<bool>(spec.before_load));
}

TEST(PanelSurfaceLifecycle, FirstConfigureOpensAndDuplicatesAreSuppressed) {
  QQmlEngine engine;
  HostQueue hosts;
  auto state = hosts.add();
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy opened(panel.get(), &PanelSurface::opened);

  ASSERT_TRUE(panel->open());
  EXPECT_TRUE(panel->isOpen());
  EXPECT_FALSE(panel->isConfigured());
  state->configured();
  EXPECT_TRUE(panel->isConfigured());
  EXPECT_EQ(opened.count(), 1);
  state->configured();
  EXPECT_EQ(opened.count(), 1);
}

TEST(PanelSurfaceLifecycle, ExplicitCloseEmitsOnceAndIsIdempotent) {
  QQmlEngine engine;
  HostQueue hosts;
  auto state = hosts.add();
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy closed(panel.get(), &PanelSurface::closed);

  ASSERT_TRUE(panel->open());
  panel->close();
  panel->close();
  QCoreApplication::processEvents();
  EXPECT_EQ(state->close_calls, 1);
  EXPECT_EQ(closed.count(), 1);
  EXPECT_FALSE(panel->isOpen());
  EXPECT_FALSE(panel->isConfigured());
  EXPECT_TRUE(panel->outputName().isEmpty());
}

TEST(PanelSurfaceLifecycle, CompositorCloseAndFailureTerminateThroughQueuedHandlers) {
  QQmlEngine engine;
  HostQueue hosts;
  auto closed_state = hosts.add();
  auto failed_state = hosts.add();
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy closed(panel.get(), &PanelSurface::closed);
  QSignalSpy failed(panel.get(), &PanelSurface::failed);

  ASSERT_TRUE(panel->open());
  closed_state->closed();
  EXPECT_TRUE(panel->isOpen());
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel->isOpen());
  EXPECT_EQ(closed.count(), 1);

  ASSERT_TRUE(panel->open());
  failed_state->failed(QStringLiteral("compositor failure"));
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel->isOpen());
  ASSERT_EQ(failed.count(), 1);
  EXPECT_EQ(failed.at(0).at(0).toString(), QStringLiteral("compositor failure"));
}

TEST(PanelSurfaceLifecycle, RejectsInvalidOutputWithoutCreatingHost) {
  QQmlEngine engine;
  HostQueue hosts;
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy failed(panel.get(), &PanelSurface::failed);

  EXPECT_FALSE(panel->open(QStringLiteral("not-a-real-output")));
  EXPECT_FALSE(panel->isOpen());
  ASSERT_EQ(failed.count(), 1);
  EXPECT_TRUE(failed.at(0).at(0).toString().contains(QStringLiteral("Unknown output")));
}

TEST(PanelSurfaceLifecycle, RepeatedOpenKeepsTheExistingHost) {
  QQmlEngine engine;
  HostQueue hosts;
  auto state = hosts.add();
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());

  ASSERT_TRUE(panel->open());
  EXPECT_TRUE(panel->open(QStringLiteral("ignored-while-open")));
  EXPECT_EQ(state->open_calls, 1);
}

TEST(PanelSurfaceLifecycle, StaleTerminalCallbacksCannotCloseReplacementHost) {
  QQmlEngine engine;
  HostQueue hosts;
  auto old_state = hosts.add();
  auto replacement_state = hosts.add();
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy closed(panel.get(), &PanelSurface::closed);
  QSignalSpy failed(panel.get(), &PanelSurface::failed);

  ASSERT_TRUE(panel->open());
  old_state->closed();
  panel->close();
  ASSERT_TRUE(panel->open());
  QCoreApplication::processEvents();

  EXPECT_TRUE(panel->isOpen());
  EXPECT_EQ(closed.count(), 1);
  EXPECT_EQ(failed.count(), 0);
  EXPECT_EQ(replacement_state->open_calls, 1);
}

TEST(PanelSurfaceLifecycle, SynchronousOpenFailurePropagatesOnce) {
  QQmlEngine engine;
  HostQueue hosts;
  auto state = hosts.add();
  state->open_result = false;
  auto panel = holonight_platform::PanelSurfaceTestAccess::create(engine, hosts.factory());
  QSignalSpy failed(panel.get(), &PanelSurface::failed);

  EXPECT_FALSE(panel->open());
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel->isOpen());
  ASSERT_EQ(failed.count(), 1);
  EXPECT_EQ(failed.at(0).at(0).toString(), state->diagnostic);
}

}  // namespace
