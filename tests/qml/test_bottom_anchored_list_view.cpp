#include <QAbstractListModel>
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

class VariableHeightModel final : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role { HeightRole = Qt::UserRole + 1 };

  explicit VariableHeightModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : static_cast<int>(heights_.size());
  }

  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || role != HeightRole) {
      return {};
    }
    return heights_[static_cast<std::size_t>(index.row())];
  }

  [[nodiscard]] QHash<int, QByteArray> roleNames() const override {
    return {{HeightRole, QByteArrayLiteral("delegateHeight")}};
  }

  void resetHeights(std::vector<int> heights) {
    beginResetModel();
    heights_ = std::move(heights);
    endResetModel();
  }

  void setNewestHeight(int height) {
    heights_.front() = height;
    emit dataChanged(index(0), index(0), {HeightRole});
  }

  void insertNewestHeight(int height) {
    beginInsertRows({}, 0, 0);
    heights_.insert(heights_.begin(), height);
    endInsertRows();
  }

 private:
  std::vector<int> heights_;
};

std::vector<int> variedHeights(int count) {
  std::vector<int> heights;
  heights.reserve(static_cast<std::size_t>(count));
  for (int row = 0; row < count; ++row) {
    heights.push_back(28 + ((row % 9) * 13));
  }
  return heights;
}

QQuickItem* delegateForRow(QQuickItem* view, int row) {
  const auto items = view->findChildren<QQuickItem*>(QStringLiteral("variableHeightDelegate"));
  for (QQuickItem* item : items) {
    if (item->property("rowIndex").toInt() == row) {
      return item;
    }
  }
  return nullptr;
}

qreal bottomInView(QQuickItem* delegate, QQuickItem* view) {
  return delegate->mapToItem(view, QPointF(0, delegate->height())).y();
}

TEST(BottomAnchoredListView, KeepsNewestAtBottomAcrossLayoutChangesAndReset) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights(variedHeights(80));
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  const QByteArray source = QStringLiteral(R"(
    import QtQuick
    import "." as Shared

    Shared.BottomAnchoredListView {
      width: 420
      height: 360
      spacing: 5
      model: heightModel
      delegate: Rectangle {
        required property int index
        required property int delegateHeight
        objectName: "variableHeightDelegate"
        property int rowIndex: index
        width: ListView.view.width
        height: delegateHeight
      }
    }
  )")
                                .toUtf8();
  QQmlComponent component(&engine);
  component.setData(
      source, QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) + QStringLiteral("/qml/shared/test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);
  EXPECT_GE(view->property("maximumFlickVelocity").toReal(), 30000.0);

  QQuickWindow window;
  window.resize(420, 360);
  view->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

  QTRY_VERIFY(delegateForRow(view, 0) != nullptr);
  QQuickItem* newest = delegateForRow(view, 0);
  ASSERT_NE(newest, nullptr);
  EXPECT_NEAR(bottomInView(newest, view), view->height(), 1.0);

  model.setNewestHeight(180);
  QTRY_COMPARE(newest->height(), 180.0);
  EXPECT_NEAR(bottomInView(newest, view), view->height(), 1.0);

  for (qreal height : {300.0, 250.0, 390.0, 320.0}) {
    view->setHeight(height);
    QCoreApplication::processEvents();
    EXPECT_NEAR(bottomInView(newest, view), height, 1.0);
  }

  ASSERT_TRUE(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
  QCoreApplication::processEvents();
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "movementStarted"));
  EXPECT_FALSE(view->property("followingLatest").toBool());
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "showLatest"));
  QTRY_VERIFY(view->property("followingLatest").toBool());
  QTRY_VERIFY(delegateForRow(view, 0) != nullptr);
  EXPECT_NEAR(bottomInView(delegateForRow(view, 0), view), view->height(), 1.0);

  model.resetHeights(variedHeights(120));
  QTRY_VERIFY(delegateForRow(view, 0) != nullptr);
  EXPECT_NEAR(bottomInView(delegateForRow(view, 0), view), view->height(), 1.0);
}

TEST(BottomAnchoredListView, StreamingUpdatesDoNotStarveEventLoop) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights(variedHeights(80));
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420; height: 360; model: heightModel
          delegate: Item { required property int delegateHeight; width: ListView.view.width; height: delegateHeight }
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/heartbeat-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> view(component.create());
  ASSERT_NE(view, nullptr) << qPrintable(component.errorString());

  int heartbeat = 0;
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&heartbeat] { ++heartbeat; });
  timer.start(0);
  for (int update = 0; update < 400; ++update) {
    model.setNewestHeight(30 + update);
    QCoreApplication::processEvents();
  }

  EXPECT_GT(heartbeat, 0);
}

TEST(BottomAnchoredListView, ExposesVerticalScrollPositionForLongTranscripts) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights(variedHeights(80));
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420
          height: 360
          model: heightModel
          delegate: Item {
            required property int delegateHeight
            width: ListView.view.width
            height: delegateHeight
          }
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/scrollbar-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);

  QQuickWindow window;
  window.resize(420, 360);
  view->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

  auto* scroll_bar = view->findChild<QObject*>(QStringLiteral("transcriptScrollBar"));
  ASSERT_NE(scroll_bar, nullptr);
  QTRY_VERIFY(scroll_bar->property("visible").toBool());
  EXPECT_LT(scroll_bar->property("size").toReal(), 1.0);

  const qreal initial_position = scroll_bar->property("position").toReal();
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
  QTRY_VERIFY(scroll_bar->property("position").toReal() != initial_position);
}

TEST(BottomAnchoredListView, RestoredTallResponseKeepsScrollbarGeometryStable) {
  QQmlEngine engine;
  VariableHeightModel model;
  std::vector<int> restored_heights{6000};
  restored_heights.insert(restored_heights.end(), 20, 160);
  model.resetHeights(std::move(restored_heights));
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420
          height: 900
          spacing: 5
          model: heightModel
          delegate: Item {
            required property int index
            required property int delegateHeight
            objectName: "variableHeightDelegate"
            property int rowIndex: index
            width: ListView.view.width
            height: delegateHeight
          }
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/restored-tall-response-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);

  QQuickWindow window;
  window.resize(420, 900);
  view->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();

  QTRY_VERIFY_WITH_TIMEOUT(delegateForRow(view, 0) != nullptr, 250);
  QTRY_VERIFY_WITH_TIMEOUT(delegateForRow(view, 1) != nullptr, 250);
  QTRY_VERIFY_WITH_TIMEOUT(delegateForRow(view, 10) != nullptr, 250);
  auto* scroll_bar = view->findChild<QObject*>(QStringLiteral("transcriptScrollBar"));
  ASSERT_NE(scroll_bar, nullptr);
  const qreal initial_size = scroll_bar->property("size").toReal();

  ASSERT_TRUE(QMetaObject::invokeMethod(view, "positionViewAtEnd"));
  const qreal restored_size = scroll_bar->property("size").toReal();

  EXPECT_NEAR(restored_size, initial_size, 0.005);
}

TEST(BottomAnchoredListView, AcceleratesHighResolutionDiscreteWheelEvents) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights({6000});
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420
          height: 900
          model: heightModel
          delegate: Item {
            required property int delegateHeight
            width: ListView.view.width
            height: delegateHeight
          }
        }
      )")
                        .toUtf8(),
                    QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) +
                                        QStringLiteral("/qml/shared/high-resolution-wheel-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);

  QQuickWindow window;
  window.resize(420, 900);
  view->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();

  auto* scroll_bar = view->findChild<QObject*>(QStringLiteral("transcriptScrollBar"));
  ASSERT_NE(scroll_bar, nullptr);
  const qreal initial_position = scroll_bar->property("position").toReal();

  bool handled = false;
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "handleDiscreteWheel", Q_RETURN_ARG(bool, handled), Q_ARG(qreal, 16.0),
                                        Q_ARG(int, static_cast<int>(Qt::NoScrollPhase))));
  EXPECT_TRUE(handled);
  QCoreApplication::processEvents();

  const qreal position_change = std::abs(scroll_bar->property("position").toReal() - initial_position);
  EXPECT_GT(position_change * view->property("contentHeight").toReal(), 80.0);

  ASSERT_TRUE(QMetaObject::invokeMethod(view, "handleDiscreteWheel", Q_RETURN_ARG(bool, handled), Q_ARG(qreal, 16.0),
                                        Q_ARG(int, static_cast<int>(Qt::ScrollUpdate))));
  EXPECT_FALSE(handled);
}

TEST(BottomAnchoredListView, UserExpansionCanPreserveDelegateTop) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights(variedHeights(20));
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(
      QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420
          height: 360
          model: heightModel
          delegate: Item {
            required property int index
            required property int delegateHeight
            objectName: "variableHeightDelegate"
            property int rowIndex: index
            width: ListView.view.width
            height: delegateHeight
          }
        }
      )")
          .toUtf8(),
      QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) + QStringLiteral("/qml/shared/expand-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);

  QQuickWindow window;
  window.resize(420, 360);
  view->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

  QTRY_VERIFY(delegateForRow(view, 0) != nullptr);
  QQuickItem* newest = delegateForRow(view, 0);
  const qreal previous_top = newest->mapToItem(view, QPointF{}).y();

  QVariant returned;
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "beginPreservingItemTop", Q_RETURN_ARG(QVariant, returned),
                                        Q_ARG(QVariant, QVariant::fromValue(newest))));
  model.setNewestHeight(240);
  QTRY_COMPARE(newest->height(), 240.0);

  ASSERT_TRUE(QMetaObject::invokeMethod(view, "finishPreservingItemTop", Q_RETURN_ARG(QVariant, returned)));

  QTRY_VERIFY(std::abs(newest->mapToItem(view, QPointF{}).y() - previous_top) <= 1.0);
}

TEST(BottomAnchoredListView, ReanchorsAfterInsertedDelegateHeightSettles) {
  QQmlEngine engine;
  VariableHeightModel model;
  model.resetHeights({80});
  engine.rootContext()->setContextProperty(QStringLiteral("heightModel"), &model);

  QQmlComponent component(&engine);
  component.setData(
      QStringLiteral(R"(
        import QtQuick
        import "." as Shared
        Shared.BottomAnchoredListView {
          width: 420
          height: 360
          model: heightModel
          delegate: Item {
            required property int index
            required property int delegateHeight
            objectName: "variableHeightDelegate"
            property int rowIndex: index
            width: ListView.view.width
            height: delegateHeight
          }
        }
      )")
          .toUtf8(),
      QUrl::fromLocalFile(QStringLiteral(PROJECT_SOURCE_DIR) + QStringLiteral("/qml/shared/insert-test-harness.qml")));
  ASSERT_TRUE(component.isReady()) << qPrintable(component.errorString());

  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << qPrintable(component.errorString());
  auto* view = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(view, nullptr);

  QQuickWindow window;
  window.resize(420, 360);
  view->setParentItem(window.contentItem());
  window.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

  model.insertNewestHeight(0);
  model.setNewestHeight(140);

  QTRY_VERIFY(delegateForRow(view, 0) != nullptr);
  QTRY_COMPARE(delegateForRow(view, 0)->height(), 140.0);
  QTRY_VERIFY(std::abs(bottomInView(delegateForRow(view, 0), view) - view->height()) <= 1.0);
}

}  // namespace

#include "test_bottom_anchored_list_view.moc"
