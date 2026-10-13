// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Proves that injected events drive REAL Qt controls, not merely that the right
// event object was built: a QPushButton fires clicked(), a QSlider's value
// follows a drag, a QComboBox opens its popup and takes a selection, and so on.
// Headless under the offscreen platform plugin. Nothing here asserts on
// rendered text — that plugin rasterizes no glyphs on this host.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_input_injector.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <thread>
#include <vector>

using qar_qt_streaming::InputDeliveryMode;
using qar_qt_streaming::PanelInputOptions;
using qar_qt_streaming::PointerAction;
using qar_qt_streaming::PointerActionKind;
using qar_qt_streaming::QtPanelInputInjector;

namespace
{

/// A shown, laid-out top-level panel widget. show() is required, not cosmetic:
/// childAt() ignores hidden children and focus needs an active window. Under
/// the offscreen platform plugin no window ever reaches a desktop.
class Panel
{
public:
  Panel(int width = 400, int height = 300)
  {
    widget_ = new QWidget();
    widget_->resize(width, height);
    widget_->move(100, 80);
  }

  ~Panel() { delete widget_; }

  /// Call once the children are in place.
  void show()
  {
    widget_->show();
    widget_->ensurePolished();
    if (widget_->layout() != nullptr) {
      widget_->layout()->activate();
    }
    QCoreApplication::processEvents();
    QApplication::setActiveWindow(widget_);
    QCoreApplication::processEvents();
  }

  QWidget * widget() const { return widget_; }

  /// An action at @p local inside @p target, expressed in panel coordinates.
  PointerAction at(
    PointerActionKind kind, QWidget * target, const QPoint & local, std::uint32_t pointer_id = 1,
    bool inside = true, std::uint32_t click_count = 1) const
  {
    const QPoint p = target->mapTo(widget_, local);
    return at(kind, QPointF(p), pointer_id, inside, click_count);
  }

  PointerAction at(
    PointerActionKind kind, const QPointF & panel_pos, std::uint32_t pointer_id = 1,
    bool inside = true, std::uint32_t click_count = 1) const
  {
    PointerAction action;
    action.kind = kind;
    action.pointer_id = pointer_id;
    action.panel_key = "panel";
    action.x = panel_pos.x();
    action.y = panel_pos.y();
    action.inside = inside;
    action.timestamp_s = 1.0;
    action.click_count = click_count;
    return action;
  }

  /// Press + release + click at the centre of @p target.
  void click(QtPanelInputInjector & injector, QWidget * target, std::uint32_t pointer_id = 1) const
  {
    const QPoint centre = target->rect().center();
    injector.deliver(at(PointerActionKind::Press, target, centre, pointer_id));
    injector.deliver(at(PointerActionKind::Release, target, centre, pointer_id));
    injector.deliver(at(PointerActionKind::Click, target, centre, pointer_id));
  }

private:
  QWidget * widget_ = nullptr;
};

/// Records the types of every event that reaches the object it filters.
class EventRecorder : public QObject
{
public:
  std::vector<QEvent::Type> types;

  bool eventFilter(QObject *, QEvent * event) override
  {
    types.push_back(event->type());
    return false;
  }

  int count(QEvent::Type type) const
  {
    int n = 0;
    for (const auto t : types) {
      if (t == type) {
        ++n;
      }
    }
    return n;
  }
};

/// A widget that speaks touch, as a kinetic-scrolling view or a custom gesture
/// widget would.
class TouchWidget : public QWidget
{
public:
  explicit TouchWidget(QWidget * parent)
  : QWidget(parent)
  {
    setAttribute(Qt::WA_AcceptTouchEvents);
  }

  std::vector<QEvent::Type> touch_types;
  std::vector<int> point_counts;
  std::vector<int> released_ids;
  int mouse_presses = 0;

protected:
  bool event(QEvent * e) override
  {
    switch (e->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd:
      case QEvent::TouchCancel: {
        auto * touch = static_cast<QTouchEvent *>(e);
        touch_types.push_back(e->type());
        point_counts.push_back(touch->touchPoints().size());
        for (const auto & point : touch->touchPoints()) {
          if (point.state() == Qt::TouchPointReleased) {
            released_ids.push_back(point.id());
          }
        }
        e->accept();
        return true;
      }
      case QEvent::MouseButtonPress:
        ++mouse_presses;
        break;
      default:
        break;
    }
    return QWidget::event(e);
  }
};

/// Exposes the style's handle rectangle so a test can press ON the handle.
class ProbeSlider : public QSlider
{
public:
  using QSlider::QSlider;

  QRect handle_rect() const
  {
    QStyleOptionSlider option;
    initStyleOption(&option);
    return style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
  }
};

/// Pump the event loop for @p ms of wall time.
void
wait_ms(int ms)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

}  // namespace

TEST(InputInjector, AClickFiresAPushButton)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(50, 50, 120, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  panel.click(injector, button);

  EXPECT_EQ(clicks, 1);
  EXPECT_FALSE(button->isDown());
  EXPECT_GE(injector.stats().delivered, 2u);
  EXPECT_EQ(injector.stats().dropped, 0u);
}

TEST(InputInjector, PressDragAwayReleaseDoesNotClick)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(50, 50, 120, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, button, button->rect().center()));
  EXPECT_TRUE(button->isDown());
  EXPECT_EQ(injector.grab_widget(1), button);

  // Off the button but still on the panel...
  injector.deliver(panel.at(PointerActionKind::Drag, QPointF(300.0, 250.0)));
  EXPECT_EQ(injector.grab_widget(1), button) << "the implicit grab holds through the drag";
  // ...and then past the panel's edge, where Qt's grab still owns the release.
  injector.deliver(panel.at(PointerActionKind::Drag, QPointF(-40.0, -40.0), 1, false));
  injector.deliver(panel.at(PointerActionKind::Release, QPointF(-40.0, -40.0), 1, false));

  EXPECT_EQ(clicks, 0);
  EXPECT_FALSE(button->isDown());
  EXPECT_EQ(injector.grab_widget(1), nullptr);
}

TEST(InputInjector, ACheckBoxToggles)
{
  Panel panel;
  auto * box = new QCheckBox("Enable", panel.widget());
  box->setGeometry(20, 20, 150, 30);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  panel.click(injector, box);
  EXPECT_TRUE(box->isChecked());
  panel.click(injector, box);
  EXPECT_FALSE(box->isChecked());
}

TEST(InputInjector, ASliderFollowsAPressAndDragMonotonically)
{
  Panel panel;
  auto * slider = new ProbeSlider(Qt::Horizontal, panel.widget());
  slider->setRange(0, 100);
  slider->setValue(0);
  slider->setGeometry(20, 100, 300, 30);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  const QPoint handle = slider->handle_rect().center();
  injector.deliver(panel.at(PointerActionKind::Press, slider, handle));

  int previous = slider->value();
  for (int dx = 30; dx <= 240; dx += 30) {
    injector.deliver(panel.at(PointerActionKind::Drag, slider, handle + QPoint(dx, 0)));
    EXPECT_GE(slider->value(), previous) << "dragging right must never move the value left";
    previous = slider->value();
  }
  injector.deliver(panel.at(PointerActionKind::Release, slider, handle + QPoint(240, 0)));

  EXPECT_GT(slider->value(), 50) << "a drag across most of the groove lands past the middle";
  EXPECT_FALSE(slider->isSliderDown());
}

TEST(InputInjector, AComboBoxOpensItsPopupAndTakesASelection)
{
  Panel panel;
  auto * combo = new QComboBox(panel.widget());
  combo->addItems({"zero", "one", "two"});
  combo->setGeometry(20, 20, 200, 30);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  panel.click(injector, combo);
  QCoreApplication::processEvents();
  ASSERT_NE(QApplication::activePopupWidget(), nullptr) << "the click did not open the list";
  EXPECT_EQ(combo->currentIndex(), 0);

  // QComboBox ignores a release inside the double-click interval after opening
  // (so a quick press-release leaves the list up); let that pass.
  wait_ms(QApplication::doubleClickInterval() + 100);

  // The list is a separate popup window. Its items are addressed through the
  // panel's frame, exactly as a pointer over the panel would be.
  QListView * view = qobject_cast<QListView *>(combo->view());
  ASSERT_NE(view, nullptr);
  const QRect item = view->visualRect(combo->model()->index(2, 0));
  const QPoint global = view->viewport()->mapToGlobal(item.center());
  const QPointF on_panel(panel.widget()->mapFromGlobal(global));

  injector.deliver(panel.at(PointerActionKind::Move, on_panel));
  injector.deliver(panel.at(PointerActionKind::Press, on_panel));
  injector.deliver(panel.at(PointerActionKind::Release, on_panel));
  QCoreApplication::processEvents();

  EXPECT_EQ(combo->currentIndex(), 2);
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr) << "a selection closes the list";
  combo->hidePopup();
}

TEST(InputInjector, APressOutsideAnOpenPopupClosesIt)
{
  Panel panel;
  auto * combo = new QComboBox(panel.widget());
  combo->addItems({"zero", "one", "two"});
  combo->setGeometry(20, 20, 200, 30);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  panel.click(injector, combo);
  QCoreApplication::processEvents();
  ASSERT_NE(QApplication::activePopupWidget(), nullptr);
  wait_ms(QApplication::doubleClickInterval() + 100);

  // Far corner of the panel: on no item, on no widget of the popup.
  injector.deliver(panel.at(PointerActionKind::Press, QPointF(390.0, 290.0)));
  injector.deliver(panel.at(PointerActionKind::Release, QPointF(390.0, 290.0)));
  QCoreApplication::processEvents();

  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(combo->currentIndex(), 0);
  combo->hidePopup();
}

TEST(InputInjector, ClickingALineEditGivesItFocus)
{
  Panel panel;
  auto * first = new QLineEdit(panel.widget());
  first->setGeometry(20, 20, 200, 30);
  auto * second = new QLineEdit(panel.widget());
  second->setGeometry(20, 80, 200, 30);
  panel.show();
  ASSERT_TRUE(panel.widget()->isActiveWindow()) << "focus needs an active window";

  QtPanelInputInjector injector(panel.widget());
  panel.click(injector, second);
  EXPECT_TRUE(second->hasFocus());
  EXPECT_FALSE(first->hasFocus());

  panel.click(injector, first);
  EXPECT_TRUE(first->hasFocus());
  EXPECT_EQ(QApplication::focusWidget(), first);
}

TEST(InputInjector, FocusPolicyCanBeLeftAlone)
{
  Panel panel;
  // Two edits: window activation hands focus to the first one on its own.
  auto * first = new QLineEdit(panel.widget());
  first->setGeometry(20, 20, 200, 30);
  auto * second = new QLineEdit(panel.widget());
  second->setGeometry(20, 80, 200, 30);
  panel.show();
  ASSERT_TRUE(first->hasFocus());

  PanelInputOptions options;
  options.apply_focus_policy = false;
  QtPanelInputInjector injector(panel.widget(), options);
  panel.click(injector, second);
  EXPECT_FALSE(second->hasFocus());
  EXPECT_TRUE(first->hasFocus());
}

TEST(InputInjector, ATabClickSwitchesTheTabWidgetPage)
{
  Panel panel;
  auto * tabs = new QTabWidget(panel.widget());
  tabs->setGeometry(10, 10, 360, 260);
  tabs->addTab(new QWidget(), "First");
  tabs->addTab(new QWidget(), "Second");
  panel.show();
  ASSERT_EQ(tabs->currentIndex(), 0);

  QtPanelInputInjector injector(panel.widget());
  const QPoint second_tab = tabs->tabBar()->tabRect(1).center();
  injector.deliver(panel.at(PointerActionKind::Press, tabs->tabBar(), second_tab));
  injector.deliver(panel.at(PointerActionKind::Release, tabs->tabBar(), second_tab));

  EXPECT_EQ(tabs->currentIndex(), 1);
}

TEST(InputInjector, NestedWidgetsInAScrolledScrollAreaResolveCorrectly)
{
  Panel panel;
  auto * area = new QScrollArea(panel.widget());
  area->setGeometry(20, 20, 240, 120);
  area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  // Three levels deep: content -> group box -> frame -> button, with the button
  // far enough down that it is only reachable after scrolling.
  auto * content = new QWidget();
  content->setFixedSize(200, 500);
  auto * group = new QGroupBox("Group", content);
  group->setGeometry(10, 300, 180, 120);
  auto * frame = new QFrame(group);
  frame->setGeometry(10, 20, 160, 90);
  auto * button = new QPushButton("Deep", frame);
  button->setGeometry(20, 20, 100, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  area->setWidget(content);
  panel.show();

  QtPanelInputInjector injector(panel.widget());

  // Before scrolling the button is out of the viewport: the same PANEL point
  // that will later hit it must hit something else now.
  area->verticalScrollBar()->setValue(0);
  QCoreApplication::processEvents();
  const QPoint before = button->mapTo(panel.widget(), button->rect().center());
  EXPECT_FALSE(area->viewport()->rect().contains(area->viewport()->mapFrom(panel.widget(), before)));

  area->verticalScrollBar()->setValue(320);
  QCoreApplication::processEvents();
  const QPoint after = button->mapTo(panel.widget(), button->rect().center());
  ASSERT_TRUE(area->viewport()->rect().contains(area->viewport()->mapFrom(panel.widget(), after)))
    << "the scroll offset must bring the button into view";

  injector.deliver(panel.at(PointerActionKind::Press, QPointF(after)));
  EXPECT_EQ(injector.grab_widget(1), button) << "childAt must resolve through the scroll offset";
  injector.deliver(panel.at(PointerActionKind::Release, QPointF(after)));
  EXPECT_EQ(clicks, 1);
}

TEST(InputInjector, HoverProducesEnterLeaveAndUnderMouse)
{
  Panel panel;
  auto * a = new QPushButton("A", panel.widget());
  a->setGeometry(20, 20, 100, 40);
  auto * b = new QPushButton("B", panel.widget());
  b->setGeometry(200, 20, 100, 40);
  a->setAttribute(Qt::WA_Hover);
  EventRecorder recorder;
  a->installEventFilter(&recorder);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Enter, a, a->rect().center()));
  injector.deliver(panel.at(PointerActionKind::Move, a, a->rect().center()));
  EXPECT_TRUE(a->underMouse());
  EXPECT_FALSE(b->underMouse());
  EXPECT_EQ(injector.hover_widget(1), a);
  EXPECT_EQ(recorder.count(QEvent::Enter), 1);
  EXPECT_EQ(recorder.count(QEvent::HoverEnter), 1);
  EXPECT_GE(recorder.count(QEvent::HoverMove), 1);

  injector.deliver(panel.at(PointerActionKind::Move, b, b->rect().center()));
  EXPECT_FALSE(a->underMouse());
  EXPECT_TRUE(b->underMouse());
  EXPECT_EQ(recorder.count(QEvent::Leave), 1);
  EXPECT_EQ(recorder.count(QEvent::HoverLeave), 1);

  injector.deliver(panel.at(PointerActionKind::Leave, b, b->rect().center()));
  EXPECT_FALSE(b->underMouse());
  EXPECT_EQ(injector.hover_widget(1), nullptr);
}

TEST(InputInjector, MouseTrackingWidgetsSeeHoverMoves)
{
  Panel panel;
  auto * tracked = new QWidget(panel.widget());
  tracked->setGeometry(20, 20, 100, 100);
  tracked->setMouseTracking(true);
  auto * untracked = new QWidget(panel.widget());
  untracked->setGeometry(200, 20, 100, 100);
  EventRecorder tracked_events;
  EventRecorder untracked_events;
  tracked->installEventFilter(&tracked_events);
  untracked->installEventFilter(&untracked_events);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Move, tracked, QPoint(10, 10)));
  injector.deliver(panel.at(PointerActionKind::Move, untracked, QPoint(10, 10)));
  EXPECT_EQ(tracked_events.count(QEvent::MouseMove), 1);
  EXPECT_EQ(untracked_events.count(QEvent::MouseMove), 0)
    << "QApplication drops button-less moves for widgets without tracking";
}

TEST(InputInjector, TwoPointersOverOneWidgetAreOneHover)
{
  Panel panel;
  auto * a = new QPushButton("A", panel.widget());
  a->setGeometry(20, 20, 100, 40);
  EventRecorder recorder;
  a->installEventFilter(&recorder);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Move, a, QPoint(10, 10), 1));
  injector.deliver(panel.at(PointerActionKind::Move, a, QPoint(50, 10), 2));
  EXPECT_EQ(recorder.count(QEvent::Enter), 1);

  injector.deliver(panel.at(PointerActionKind::Leave, a, QPoint(10, 10), 1));
  EXPECT_TRUE(a->underMouse()) << "pointer 2 is still there";
  injector.deliver(panel.at(PointerActionKind::Leave, a, QPoint(50, 10), 2));
  EXPECT_FALSE(a->underMouse());
  EXPECT_EQ(recorder.count(QEvent::Leave), 1);
}

TEST(InputInjector, OnlyOnePointerSynthesizesTheMouse)
{
  Panel panel;
  auto * a = new QPushButton("A", panel.widget());
  a->setGeometry(20, 20, 100, 40);
  auto * b = new QPushButton("B", panel.widget());
  b->setGeometry(200, 20, 100, 40);
  int a_clicks = 0;
  int b_clicks = 0;
  QObject::connect(a, &QPushButton::clicked, [&a_clicks]() { ++a_clicks; });
  QObject::connect(b, &QPushButton::clicked, [&b_clicks]() { ++b_clicks; });
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  EXPECT_TRUE(injector.deliver(panel.at(PointerActionKind::Press, a, a->rect().center(), 1)));
  std::uint32_t owner = 0;
  ASSERT_TRUE(injector.mouse_pointer(owner));
  EXPECT_EQ(owner, 1u);

  // A second hand pressing a plain button while the first holds the mouse:
  // there is only one mouse, and QPushButton does not speak touch.
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Press, b, b->rect().center(), 2)));
  EXPECT_EQ(injector.stats().dropped, 1u);
  EXPECT_NE(injector.stats().last_drop_reason.find("second pointer"), std::string::npos);
  EXPECT_FALSE(b->isDown());

  EXPECT_TRUE(injector.deliver(panel.at(PointerActionKind::Release, a, a->rect().center(), 1)));
  EXPECT_EQ(a_clicks, 1);
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Release, b, b->rect().center(), 2)));
  EXPECT_EQ(b_clicks, 0);

  // With the mouse free again, pointer 2 can own it.
  panel.click(injector, b, 2);
  EXPECT_EQ(b_clicks, 1);
}

TEST(InputInjector, TouchAwareWidgetsGetTouchSequencesWithEveryPoint)
{
  Panel panel;
  auto * touch = new TouchWidget(panel.widget());
  touch->setGeometry(20, 20, 200, 200);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, touch, QPoint(30, 30), 1));
  EXPECT_EQ(injector.grab_widget(1), touch);
  injector.deliver(panel.at(PointerActionKind::Drag, touch, QPoint(40, 40), 1));
  injector.deliver(panel.at(PointerActionKind::Press, touch, QPoint(120, 120), 2));
  injector.deliver(panel.at(PointerActionKind::Release, touch, QPoint(40, 40), 1));
  injector.deliver(panel.at(PointerActionKind::Release, touch, QPoint(120, 120), 2));

  const std::vector<QEvent::Type> expected{
    QEvent::TouchBegin,   // pointer 1 down
    QEvent::TouchUpdate,  // pointer 1 moves
    QEvent::TouchUpdate,  // pointer 2 down: two points, one pressed one stationary
    QEvent::TouchUpdate,  // pointer 1 up: two points, one released one stationary
    QEvent::TouchEnd};    // pointer 2 up: the last point
  EXPECT_EQ(touch->touch_types, expected);
  EXPECT_EQ(touch->point_counts, (std::vector<int>{1, 1, 2, 2, 1}));
  EXPECT_EQ(touch->released_ids, (std::vector<int>{1, 2}));
  EXPECT_EQ(touch->mouse_presses, 0) << "a widget that took the touch gets no mouse";
  std::uint32_t owner = 0;
  EXPECT_FALSE(injector.mouse_pointer(owner));
}

TEST(InputInjector, TouchOnlyModeGivesPlainWidgetsNothing)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  panel.show();

  PanelInputOptions options;
  options.delivery_mode = InputDeliveryMode::TouchOnly;
  QtPanelInputInjector injector(panel.widget(), options);
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Press, button, button->rect().center())));
  EXPECT_NE(injector.stats().last_drop_reason.find("touch"), std::string::npos);
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Release, button, button->rect().center())));
  EXPECT_EQ(clicks, 0);
  EXPECT_FALSE(button->isDown());
}

TEST(InputInjector, MouseOnlyModeNeverSendsTouch)
{
  Panel panel;
  auto * touch = new TouchWidget(panel.widget());
  touch->setGeometry(20, 20, 200, 200);
  panel.show();

  PanelInputOptions options;
  options.delivery_mode = InputDeliveryMode::MouseOnly;
  QtPanelInputInjector injector(panel.widget(), options);
  injector.deliver(panel.at(PointerActionKind::Press, touch, QPoint(30, 30)));
  injector.deliver(panel.at(PointerActionKind::Release, touch, QPoint(30, 30)));
  EXPECT_TRUE(touch->touch_types.empty());
  EXPECT_EQ(touch->mouse_presses, 1);
}

TEST(InputInjector, ACancelReleasesAButtonWithoutClicking)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, button, button->rect().center()));
  ASSERT_TRUE(button->isDown());
  injector.deliver(panel.at(PointerActionKind::Cancel, button, button->rect().center()));
  EXPECT_FALSE(button->isDown());
  EXPECT_EQ(clicks, 0);
  EXPECT_EQ(injector.grab_widget(1), nullptr);
}

TEST(InputInjector, ACancelReachesTouchWidgetsAsTouchCancel)
{
  Panel panel;
  auto * touch = new TouchWidget(panel.widget());
  touch->setGeometry(20, 20, 200, 200);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, touch, QPoint(30, 30)));
  injector.deliver(panel.at(PointerActionKind::Cancel, touch, QPoint(30, 30)));
  ASSERT_EQ(touch->touch_types.size(), 2u);
  EXPECT_EQ(touch->touch_types.back(), QEvent::TouchCancel);
}

TEST(InputInjector, ADoubleClickPressAlsoSendsMouseButtonDblClick)
{
  Panel panel;
  auto * target = new QWidget(panel.widget());
  target->setGeometry(20, 20, 100, 100);
  EventRecorder recorder;
  target->installEventFilter(&recorder);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, target, QPoint(10, 10), 1, true, 2));
  injector.deliver(panel.at(PointerActionKind::Release, target, QPoint(10, 10)));
  EXPECT_EQ(recorder.count(QEvent::MouseButtonPress), 1);
  EXPECT_EQ(recorder.count(QEvent::MouseButtonDblClick), 1);
  EXPECT_EQ(recorder.count(QEvent::MouseButtonRelease), 1);
}

TEST(InputInjector, DeliveryOffTheGuiThreadIsRefused)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  const PointerAction press = panel.at(PointerActionKind::Press, button, button->rect().center());
  bool result = true;
  std::thread worker([&]() { result = injector.deliver(press); });
  worker.join();

  EXPECT_FALSE(result);
  EXPECT_EQ(injector.stats().dropped, 1u);
  EXPECT_NE(injector.stats().last_drop_reason.find("GUI thread"), std::string::npos);
  EXPECT_FALSE(button->isDown());
}

TEST(InputInjector, ADestroyedPanelWidgetIsHandled)
{
  auto * widget = new QWidget();
  widget->resize(100, 100);
  QtPanelInputInjector injector(widget);
  delete widget;

  PointerAction press;
  press.kind = PointerActionKind::Press;
  EXPECT_FALSE(injector.deliver(press));
  EXPECT_NE(injector.stats().last_drop_reason.find("destroyed"), std::string::npos);
  EXPECT_EQ(injector.panel_widget(), nullptr);
}

TEST(InputInjector, AGrabbedWidgetDestroyedMidDragIsHandled)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Press, button, button->rect().center()));
  delete button;

  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Drag, QPointF(50.0, 50.0))));
  EXPECT_NE(injector.stats().last_drop_reason.find("destroyed"), std::string::npos);
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Release, QPointF(50.0, 50.0))));
  EXPECT_EQ(injector.grab_widget(1), nullptr);
  std::uint32_t owner = 0;
  EXPECT_FALSE(injector.mouse_pointer(owner)) << "the mouse must be freed with the grab";
}

TEST(InputInjector, ResetLetsGoOfEverything)
{
  Panel panel;
  auto * a = new QPushButton("A", panel.widget());
  a->setGeometry(20, 20, 100, 40);
  auto * b = new QPushButton("B", panel.widget());
  b->setGeometry(200, 20, 100, 40);
  int b_clicks = 0;
  QObject::connect(b, &QPushButton::clicked, [&b_clicks]() { ++b_clicks; });
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Move, a, a->rect().center(), 1));
  injector.deliver(panel.at(PointerActionKind::Press, b, b->rect().center(), 2));
  ASSERT_TRUE(a->underMouse());
  ASSERT_TRUE(b->isDown());

  injector.reset();
  EXPECT_FALSE(a->underMouse());
  EXPECT_FALSE(b->isDown());
  EXPECT_EQ(b_clicks, 0);
  EXPECT_EQ(injector.grab_widget(2), nullptr);
  EXPECT_EQ(injector.hover_widget(1), nullptr);
}

TEST(InputInjector, DisabledInputDropsEverything)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  panel.show();

  PanelInputOptions options;
  options.enabled = false;
  QtPanelInputInjector injector(panel.widget(), options);
  EXPECT_FALSE(injector.deliver(panel.at(PointerActionKind::Press, button, button->rect().center())));
  EXPECT_FALSE(button->isDown());
  EXPECT_EQ(injector.stats().dropped, 1u);
}

TEST(InputInjector, HoverCanBeSwitchedOffWhilePressesStillWork)
{
  Panel panel;
  auto * button = new QPushButton("Go", panel.widget());
  button->setGeometry(20, 20, 100, 40);
  int clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&clicks]() { ++clicks; });
  EventRecorder recorder;
  button->installEventFilter(&recorder);
  panel.show();

  PanelInputOptions options;
  options.deliver_hover = false;
  QtPanelInputInjector injector(panel.widget(), options);
  injector.deliver(panel.at(PointerActionKind::Move, button, button->rect().center()));
  EXPECT_EQ(recorder.count(QEvent::MouseMove), 0);
  EXPECT_EQ(recorder.count(QEvent::Enter), 0);
  panel.click(injector, button);
  EXPECT_EQ(clicks, 1);
}

TEST(InputInjector, DeliveryModesHaveNames)
{
  EXPECT_STREQ(qar_qt_streaming::to_string(InputDeliveryMode::TouchWithMouseFallback), "TouchWithMouseFallback");
  EXPECT_STREQ(qar_qt_streaming::to_string(InputDeliveryMode::MouseOnly), "MouseOnly");
  EXPECT_STREQ(qar_qt_streaming::to_string(InputDeliveryMode::TouchOnly), "TouchOnly");
}

// ---------------------------------------------------------------------------
// Dialogs in the input path.
//
// An owned dialog is NOT drawn into this panel (it is streamed as a panel of its
// own), so the hit test must not route into it either: a point on this
// panel can only ever mean a widget of this panel. What survives is modality —
// while an owned APPLICATION-MODAL dialog is up, this panel takes no input at
// all, which is what Qt does for a real mouse in a platform layer this injector
// deliberately bypasses.
//
// No exec() anywhere: setModal(true) + show() gives the same modality state
// (QApplication::activeModalWidget picks it up) without a nested event loop the
// test runner could never get out of.
// ---------------------------------------------------------------------------

namespace
{

/// A dialog owned by @p parent, its whole client area one button, positioned by
/// its CONTENT origin at @p panel_local so a test can aim at panel pixels.
class DialogFixture
{
public:
  DialogFixture(QWidget * parent, const QPoint & panel_local, const QSize & size, bool modal)
  : parent_(parent)
  {
    dialog_ = new QDialog(parent);
    auto * layout = new QVBoxLayout(dialog_);
    layout->setContentsMargins(0, 0, 0, 0);
    button_ = new QPushButton("Dialog", dialog_);
    layout->addWidget(button_);
    QObject::connect(button_, &QPushButton::clicked, [this]() { ++clicks_; });
    dialog_->resize(size);
    dialog_->setModal(modal);
    dialog_->show();
    dialog_->layout()->activate();
    QCoreApplication::processEvents();
    place(panel_local);
  }

  ~DialogFixture()
  {
    dialog_->hide();
    QCoreApplication::processEvents();
    delete dialog_;
    QCoreApplication::processEvents();
  }

  QDialog * dialog() const { return dialog_; }
  int clicks() const { return clicks_; }

private:
  /// move() positions the window FRAME; the capture and the hit test both work
  /// from the CONTENT origin, so correct for the offscreen plugin's 2 px frame.
  void place(const QPoint & panel_local)
  {
    dialog_->move(parent_->mapToGlobal(panel_local));
    QCoreApplication::processEvents();
    const QPoint landed = parent_->mapFromGlobal(dialog_->mapToGlobal(QPoint(0, 0)));
    if (landed != panel_local) {
      dialog_->move(dialog_->pos() + (panel_local - landed));
      QCoreApplication::processEvents();
    }
  }

  QWidget * parent_ = nullptr;
  QDialog * dialog_ = nullptr;
  QPushButton * button_ = nullptr;
  int clicks_ = 0;
};

/// One action at a bare panel coordinate, for an injector whose panel is named
/// by the caller (the dialog's own injector, say).
PointerAction
panel_action_at(PointerActionKind kind, const QPointF & at, std::uint32_t pointer_id = 1)
{
  PointerAction action;
  action.kind = kind;
  action.pointer_id = pointer_id;
  action.panel_key = "panel";
  action.x = at.x();
  action.y = at.y();
  action.inside = true;
  action.timestamp_s = 1.0;
  action.click_count = 1;
  return action;
}

/// Press + release + click at a bare panel coordinate (not at a widget centre:
/// the whole point is that the widget under it is in another window).
void
tap(
  QtPanelInputInjector & injector, const Panel & panel, const QPointF & at,
  std::uint32_t pointer_id = 1)
{
  injector.deliver(panel.at(PointerActionKind::Press, at, pointer_id));
  injector.deliver(panel.at(PointerActionKind::Release, at, pointer_id));
  injector.deliver(panel.at(PointerActionKind::Click, at, pointer_id));
}

}  // namespace

TEST(DialogInput, APressWhereAModelessDialogSitsStaysOnThePanel)
{
  Panel panel;
  auto * behind = new QPushButton("Behind", panel.widget());
  behind->setGeometry(0, 0, 400, 300);
  int behind_clicks = 0;
  QObject::connect(behind, &QPushButton::clicked, [&behind_clicks]() { ++behind_clicks; });
  panel.show();

  // Covers panel (20, 20) to (139, 99) on the DESKTOP. In AR it covers nothing:
  // this panel's image does not contain it, so a tap there must land on the
  // widget the operator can actually see, which is this panel's own button.
  DialogFixture dialog(panel.widget(), QPoint(20, 20), QSize(120, 80), false);
  QtPanelInputInjector injector(panel.widget());

  tap(injector, panel, QPointF(60.0, 50.0));

  EXPECT_EQ(dialog.clicks(), 0) << "a tap reached a window this panel never drew";
  EXPECT_EQ(behind_clicks, 1);
}

TEST(DialogInput, TheDialogTakesInputThroughAnInjectorOfItsOwn)
{
  // The other half: the dialog is a panel in its own right, so it gets an
  // injector whose panel IS the dialog, and the ordinary resolve path applies.
  Panel panel;
  panel.show();
  DialogFixture dialog(panel.widget(), QPoint(20, 20), QSize(120, 80), false);

  QtPanelInputInjector into_dialog(dialog.dialog());
  // Dialog-local coordinates now, because the dialog is the panel.
  into_dialog.deliver(panel_action_at(PointerActionKind::Press, QPointF(60.0, 40.0)));
  into_dialog.deliver(panel_action_at(PointerActionKind::Release, QPointF(60.0, 40.0)));
  into_dialog.deliver(panel_action_at(PointerActionKind::Click, QPointF(60.0, 40.0)));

  EXPECT_EQ(dialog.clicks(), 1);
}

TEST(DialogInput, AModalDialogBlocksThePanelBehindIt)
{
  Panel panel;
  auto * behind = new QPushButton("Behind", panel.widget());
  behind->setGeometry(0, 0, 400, 300);
  int behind_clicks = 0;
  QObject::connect(behind, &QPushButton::clicked, [&behind_clicks]() { ++behind_clicks; });
  panel.show();

  DialogFixture dialog(panel.widget(), QPoint(120, 60), QSize(70, 50), true);
  QtPanelInputInjector injector(panel.widget());

  // A press well clear of the dialog: Qt delivers this to nobody for a real mouse.
  tap(injector, panel, QPointF(20.0, 20.0));

  EXPECT_EQ(behind_clicks, 0);
  EXPECT_EQ(dialog.clicks(), 0);
  EXPECT_GT(injector.stats().dropped, 0u);
  EXPECT_EQ(
    std::string(injector.stats().last_drop_reason),
    std::string("blocked by a modal dialog owned by this panel"));

  // And a press over the patch of desktop the dialog happens to occupy is
  // blocked just the same: this panel does not draw the dialog, so that point
  // shows the operator this panel's own widget, and a widget behind a modal
  // window is not clickable. The dialog is answered on its OWN panel.
  tap(injector, panel, QPointF(150.0, 80.0));
  EXPECT_EQ(dialog.clicks(), 0);
  EXPECT_EQ(behind_clicks, 0);

  QtPanelInputInjector into_dialog(dialog.dialog());
  into_dialog.deliver(panel_action_at(PointerActionKind::Press, QPointF(30.0, 25.0)));
  into_dialog.deliver(panel_action_at(PointerActionKind::Release, QPointF(30.0, 25.0)));
  into_dialog.deliver(panel_action_at(PointerActionKind::Click, QPointF(30.0, 25.0)));
  EXPECT_EQ(dialog.clicks(), 1) << "the modal rule locked the operator out of its own dialog";
}

TEST(DialogInput, AHoverOutsideAModalDialogLeavesRatherThanDrops)
{
  Panel panel;
  auto * behind = new QPushButton("Behind", panel.widget());
  behind->setGeometry(0, 0, 400, 300);
  panel.show();

  QtPanelInputInjector injector(panel.widget());
  injector.deliver(panel.at(PointerActionKind::Move, QPointF(100.0, 60.0)));
  ASSERT_EQ(injector.hover_widget(1), behind);

  DialogFixture dialog(panel.widget(), QPoint(0, 0), QSize(40, 30), true);
  const std::uint64_t dropped_before = injector.stats().dropped;

  injector.deliver(panel.at(PointerActionKind::Move, QPointF(150.0, 100.0)));

  EXPECT_EQ(injector.hover_widget(1), nullptr);
  EXPECT_EQ(injector.stats().dropped, dropped_before) << "a Leave, not a drop";
  EXPECT_FALSE(behind->underMouse());
}

TEST(DialogInput, AModalBlockIsLiftedWhenTheDialogCloses)
{
  Panel panel;
  auto * behind = new QPushButton("Behind", panel.widget());
  behind->setGeometry(0, 0, 400, 300);
  int behind_clicks = 0;
  QObject::connect(behind, &QPushButton::clicked, [&behind_clicks]() { ++behind_clicks; });
  panel.show();

  {
    DialogFixture dialog(panel.widget(), QPoint(120, 60), QSize(70, 50), true);
    QtPanelInputInjector blocked(panel.widget());
    tap(blocked, panel, QPointF(20.0, 20.0));
    ASSERT_EQ(behind_clicks, 0);
  }

  QtPanelInputInjector injector(panel.widget());
  tap(injector, panel, QPointF(20.0, 20.0));
  EXPECT_EQ(behind_clicks, 1);
}

// ---------------------------------------------------------------------------
// Input into a dialog attributed to this panel.
//
// rviz_common creates its dialogs with the MAIN WINDOW as parent, so the parent
// walk from an Add-display box never reaches the streamed Displays dock. The
// manager's sweep attributes such a dialog to the panel that most recently took
// an activating AR action, and gives the dialog a panel of its OWN.
//
// What that leaves for the injector is modality and nothing else: the panel a
// dialog is attributed to must not be clickable while that dialog is modal, and
// a panel it is NOT attributed to must not be blocked by somebody else's box.
// The injector never decides an attribution of its own: it reads. Otherwise a
// press could adopt a dialog and block a panel in the same instant, before the
// operator had been shown anything.
// ---------------------------------------------------------------------------

namespace
{

using qar_qt_streaming::PanelOverlayAttribution;

/// Two dock-like panels inside ONE top-level window, each filled by a button, so
/// "did the click reach the panel behind" is a counter rather than a guess.
class SharedWindowPanels
{
public:
  SharedWindowPanels()
  {
    // Process-wide attribution state: cleared BEFORE the injectors of this test
    // register themselves, and again after, so no test inherits a decision.
    PanelOverlayAttribution::instance().reset();
    window_ = new QWidget();
    window_->resize(700, 240);
    window_->move(100, 80);
    left_ = new QWidget(window_);
    left_->setGeometry(0, 0, 300, 200);
    right_ = new QWidget(window_);
    right_->setGeometry(340, 0, 300, 200);
    left_button_ = new QPushButton("Left", left_);
    left_button_->setGeometry(0, 0, 300, 200);
    right_button_ = new QPushButton("Right", right_);
    right_button_->setGeometry(0, 0, 300, 200);
    QObject::connect(left_button_, &QPushButton::clicked, [this]() { ++left_clicks_; });
    QObject::connect(right_button_, &QPushButton::clicked, [this]() { ++right_clicks_; });
    window_->show();
    window_->ensurePolished();
    QCoreApplication::processEvents();
    QApplication::setActiveWindow(window_);
    QCoreApplication::processEvents();
  }

  ~SharedWindowPanels()
  {
    if (dialog_ != nullptr) {
      dialog_->hide();
      QCoreApplication::processEvents();
      delete dialog_;
    }
    delete window_;
    QCoreApplication::processEvents();
    PanelOverlayAttribution::instance().reset();
  }

  QWidget * left() const { return left_; }
  QDialog * dialog() const { return dialog_; }
  QPushButton * dialog_button() const { return dialog_button_; }
  QWidget * right() const { return right_; }
  int left_clicks() const { return left_clicks_; }
  int right_clicks() const { return right_clicks_; }
  int dialog_clicks() const { return dialog_clicks_; }

  /// A dialog parented to the WINDOW — the RViz parenting — placed by its
  /// CONTENT origin at @p left_local and wide enough to straddle both panels.
  void open_window_dialog(const QPoint & left_local, const QSize & size, bool modal)
  {
    dialog_ = new QDialog(window_);
    // Geometry, not a layout: a QVBoxLayout centres a button at its size hint,
    // leaving most of the dialog bare, and every assertion here is about which
    // WINDOW a point reaches. A button filling the client area makes "it went to
    // the dialog" and "it went to the panel behind" two different counters.
    dialog_button_ = new QPushButton("Dialog", dialog_);
    dialog_button_->setGeometry(QRect(QPoint(0, 0), size));
    QObject::connect(dialog_button_, &QPushButton::clicked, [this]() { ++dialog_clicks_; });
    dialog_->resize(size);
    dialog_->setModal(modal);
    dialog_->show();
    QCoreApplication::processEvents();
    // move() positions the window FRAME; every coordinate here is a content
    // coordinate, so correct for the offscreen plugin's frame.
    dialog_->move(left_->mapToGlobal(left_local));
    QCoreApplication::processEvents();
    const QPoint landed = left_->mapFromGlobal(dialog_->mapToGlobal(QPoint(0, 0)));
    if (landed != left_local) {
      dialog_->move(dialog_->pos() + (left_local - landed));
      QCoreApplication::processEvents();
    }
  }

  /// What the manager's dialog sweep does every tick — and the only path that
  /// TAKES an attribution decision, because promoting the dialog to a panel is
  /// what puts it in front of the operator.
  static void composite_scan(QWidget * panel)
  {
    qar_qt_streaming::PanelOverlayOptions options;
    options.popups = false;
    options.tooltips = false;
    qar_qt_streaming::collect_panel_overlays(panel, options);
  }

private:
  QWidget * window_ = nullptr;
  QWidget * left_ = nullptr;
  QWidget * right_ = nullptr;
  QPushButton * left_button_ = nullptr;
  QPushButton * right_button_ = nullptr;
  QDialog * dialog_ = nullptr;
  QPushButton * dialog_button_ = nullptr;
  int left_clicks_ = 0;
  int right_clicks_ = 0;
  int dialog_clicks_ = 0;
};

PointerAction
panel_action(PointerActionKind kind, const QPointF & panel_pos, std::uint32_t pointer_id = 1)
{
  PointerAction action;
  action.kind = kind;
  action.pointer_id = pointer_id;
  action.panel_key = "panel";
  action.x = panel_pos.x();
  action.y = panel_pos.y();
  action.inside = true;
  action.timestamp_s = 1.0;
  action.click_count = 1;
  return action;
}

/// Press + release + click at a bare panel coordinate.
void
tap_panel_point(
  QtPanelInputInjector & injector, const QPointF & panel_pos, std::uint32_t pointer_id = 1)
{
  injector.deliver(panel_action(PointerActionKind::Press, panel_pos, pointer_id));
  injector.deliver(panel_action(PointerActionKind::Release, panel_pos, pointer_id));
  injector.deliver(panel_action(PointerActionKind::Click, panel_pos, pointer_id));
}

}  // namespace

TEST(DialogAttribution, AnAttributedDialogTakesInputThroughItsOwnPanelNotItsOwners)
{
  SharedWindowPanels app;
  QtPanelInputInjector left(app.left());
  QtPanelInputInjector right(app.right());

  // The operator works the left dock. This is the activation the attribution is
  // based on, and in RViz it is the press on Add that opens the box.
  tap_panel_point(left, QPointF(20.0, 20.0));
  ASSERT_EQ(app.left_clicks(), 1);

  // Content origin at left-local (200, 40), 300 x 120: it covers the right edge
  // of the left panel and the left edge of the right one.
  app.open_window_dialog(QPoint(200, 40), QSize(300, 120), false);
  // Neither dock is on its parent chain — the whole reason attribution exists.
  ASSERT_FALSE(qar_qt_streaming::panel_tree_owns_popup(app.left(), app.dialog()));
  ASSERT_FALSE(qar_qt_streaming::panel_tree_owns_popup(app.right(), app.dialog()));
  SharedWindowPanels::composite_scan(app.left());
  ASSERT_TRUE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), app.dialog(), false));

  // The owner panel never routes into it: the dialog is not in this panel's
  // image, so a tap where it happens to sit on the desktop lands on the widget
  // the operator can see there.
  tap_panel_point(left, QPointF(250.0, 80.0));
  EXPECT_EQ(app.dialog_clicks(), 0);
  EXPECT_EQ(app.left_clicks(), 2);

  // Nor does the panel it is NOT attributed to.
  tap_panel_point(right, QPointF(60.0, 80.0));
  EXPECT_EQ(app.dialog_clicks(), 0);
  EXPECT_EQ(app.right_clicks(), 1);

  // It answers through the injector of its own panel, in its own coordinates.
  QtPanelInputInjector into_dialog(app.dialog());
  tap_panel_point(into_dialog, QPointF(50.0, 60.0));
  EXPECT_EQ(app.dialog_clicks(), 1);
}

TEST(DialogAttribution, InputOnTheOtherPanelDoesNotMoveAnOpenDialog)
{
  SharedWindowPanels app;
  QtPanelInputInjector left(app.left());
  QtPanelInputInjector right(app.right());

  tap_panel_point(left, QPointF(20.0, 20.0));
  app.open_window_dialog(QPoint(200, 40), QSize(300, 120), false);
  SharedWindowPanels::composite_scan(app.left());
  ASSERT_TRUE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), app.dialog(), false));

  // The operator keeps using the other panel while the dialog is up. Even a
  // scan that is ALLOWED to decide must not re-open a settled question — the
  // answer drives where the dialog's panel hangs and which panel is blocked
  // behind it, and neither may move under the operator mid-interaction.
  for (int i = 0; i < 3; ++i) {
    tap_panel_point(right, QPointF(20.0, 20.0));
    SharedWindowPanels::composite_scan(app.right());
  }
  ASSERT_EQ(app.right_clicks(), 3);

  EXPECT_TRUE(qar_qt_streaming::panel_tree_owns_overlay(app.left(), app.dialog(), false));
  EXPECT_FALSE(qar_qt_streaming::panel_tree_owns_overlay(app.right(), app.dialog(), false))
    << "it stopped belonging to the panel that opened it";
}

TEST(DialogAttribution, AModalDialogNoSweepHasClaimedBlocksNobody)
{
  // The see-before-block invariant: a dialog only starts refusing a panel's AR
  // input once the sweep has claimed it, because that is when the operator is
  // given a panel showing it. Until then the panel behaves as if the dialog were
  // not there.
  SharedWindowPanels app;
  QtPanelInputInjector left(app.left());

  tap_panel_point(left, QPointF(20.0, 20.0));
  app.open_window_dialog(QPoint(200, 40), QSize(300, 120), true);

  tap_panel_point(left, QPointF(20.0, 20.0));
  EXPECT_EQ(app.left_clicks(), 2) << "a press blocked a panel for a dialog nobody had shown";

  // Once the sweep claims it, the very same tap is refused.
  SharedWindowPanels::composite_scan(app.left());
  tap_panel_point(left, QPointF(20.0, 20.0));
  EXPECT_EQ(app.left_clicks(), 2);
  EXPECT_EQ(
    std::string(left.stats().last_drop_reason),
    std::string("blocked by a modal dialog owned by this panel"));
}

TEST(DialogAttribution, AModalDialogBlocksOnlyThePanelItIsAttributedTo)
{
  SharedWindowPanels app;
  QtPanelInputInjector left(app.left());
  QtPanelInputInjector right(app.right());

  tap_panel_point(left, QPointF(20.0, 20.0));
  ASSERT_EQ(app.left_clicks(), 1);
  app.open_window_dialog(QPoint(200, 40), QSize(300, 120), true);
  SharedWindowPanels::composite_scan(app.left());

  // The panel the dialog belongs to: Qt gives a real mouse nothing behind an
  // application-modal window, and neither may the AR pointer — anywhere on it.
  tap_panel_point(left, QPointF(20.0, 20.0));
  EXPECT_EQ(app.left_clicks(), 1);
  EXPECT_GT(left.stats().dropped, 0u);
  EXPECT_EQ(
    std::string(left.stats().last_drop_reason),
    std::string("blocked by a modal dialog owned by this panel"));

  // ...and the dialog's OWN panel is not blocked by itself, which is what makes
  // the box answerable at all. This is the pair that has to hold together.
  QtPanelInputInjector into_dialog(app.dialog());
  tap_panel_point(into_dialog, QPointF(50.0, 60.0));
  EXPECT_EQ(app.dialog_clicks(), 1);
  EXPECT_EQ(into_dialog.stats().dropped, 0u);

  // The other panel is not showing it, so it is not blocked by it either — the
  // rule that keeps another part of RViz's modal dialogs from swallowing this
  // panel's AR clicks, applied to an attributed one.
  const std::uint64_t right_dropped = right.stats().dropped;
  tap_panel_point(right, QPointF(20.0, 20.0));
  EXPECT_EQ(app.right_clicks(), 1);
  EXPECT_EQ(right.stats().dropped, right_dropped);
}
