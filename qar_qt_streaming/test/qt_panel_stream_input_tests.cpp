// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The whole input path end to end: a pointer sample enters through the
// seam, the manager resolves it against the live panels (honouring the grab),
// the state machine turns it into actions, and the injector presses a REAL
// QPushButton. Plus the render-scale plumbing into the geometry.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <string>
#include <vector>

using qar_qt_streaming::apply_readability;
using qar_qt_streaming::PanelPointerEvent;
using qar_ar_core::PanelPointerSample;
using qar_qt_streaming::PanelStreamOptions;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::QueuedPanelInputSource;
using qar_qt_streaming::ReadabilityRecommendation;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordingPanelInputSink;

namespace
{

/// A 200 x 120 panel whose whole area is one QPushButton, so any point on the
/// panel is a point on the button.
class ButtonPanel
{
public:
  ButtonPanel()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    button_ = new QPushButton("Go", widget_);
    layout->addWidget(button_);
    QObject::connect(button_, &QPushButton::clicked, [this]() { ++clicks_; });
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~ButtonPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }
  QPushButton * button() const { return button_; }
  int clicks() const { return clicks_; }

private:
  QWidget * widget_ = nullptr;
  QPushButton * button_ = nullptr;
  int clicks_ = 0;
};

PanelStreamOptions
options_at(float z = 0.0f)
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, z};
  return options;
}

QtPanelStreamingConfig
make_config(std::shared_ptr<MockQarPanelSenderFactory> factory)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  config.clock = std::make_shared<ManualClock>();
  return config;
}

PanelPointerSample
click_at(PointerPhase phase, const QarVector3 & point, std::uint32_t pointer_id = 1, double t = 0.0)
{
  PanelPointerSample sample;
  sample.pointer_id = pointer_id;
  sample.gesture = PointerGesture::Click;
  sample.phase = phase;
  sample.timestamp_s = t;
  sample.has_room_point = true;
  sample.room_point = point;
  return sample;
}

/// Room-space point at a panel's visual CENTRE.
///
/// `PanelPlacement::pose` is QarOS panel space, where the origin is the
/// midpoint of the TOP EDGE and the surface hangs below it (as the SDK documents
/// it from the 2026-09-21 drop on).
/// So "the middle of the panel" is half a panel height BELOW the pose, and the
/// height is only known after a tick has derived it from the widget.
QarVector3
panel_centre(const QtPanelStreamManager & manager, const std::string & key, float z = 0.0f)
{
  for (const auto & panel : manager.diagnostics().panels) {
    if (panel.key == key) {
      return QarVector3{0.0f, -0.5f * panel.height_meters, z};
    }
  }
  ADD_FAILURE() << "no panel named " << key << " in the diagnostics";
  return QarVector3{0.0f, 0.0f, z};
}

}  // namespace

TEST(PanelStreamInput, RenderScaleSupersamplesTheBufferButNotThePhysicalSize)
{
  ButtonPanel panel;
  PanelStreamOptions options = options_at();
  options.render_scale = 2.0;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", panel.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_EQ(desc.buffer_width, 400u);
  EXPECT_EQ(desc.buffer_height, 240u);
  // What QarOS receives is always 1.0 (a ZOOM; see panel_geometry.hpp) - the
  // supersample above is real, it just travels in diagnostics, never here.
  EXPECT_FLOAT_EQ(desc.placement.size.content_scale, 1.0f);
  EXPECT_DOUBLE_EQ(stream.diagnostics().device_pixel_ratio, 2.0);
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-4f) << "200 px * 0.0012 m, unchanged";
  EXPECT_EQ(factory->latest()->frames().front().width, 400u);
}

TEST(PanelStreamInput, ApplyReadabilityFillsScaleAndMetres)
{
  ReadabilityRecommendation recommendation;
  recommendation.meters_per_logical_px = 0.0005;
  recommendation.render_scale = 1.5;
  PanelStreamOptions options;
  apply_readability(recommendation, options);
  EXPECT_DOUBLE_EQ(options.meters_per_logical_px, 0.0005);
  EXPECT_DOUBLE_EQ(options.render_scale, 1.5);
}

TEST(PanelStreamInput, AnInstantClickThroughTheManagerPressesTheRealButton)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingPanelInputSink sink;
  QtPanelStreamingConfig config = make_config(factory);
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();  // derives the panel's physical size

  EXPECT_TRUE(manager.handle_sample(click_at(PointerPhase::Instant, panel_centre(manager, "p"))));
  EXPECT_EQ(panel.clicks(), 1);

  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.input_samples, 1u);
  ASSERT_EQ(snapshot.panels.size(), 1u);
  // Counted against the panel the sample RESOLVED to, before the state machine
  // or the injector could have discarded it: the number an operator reads to
  // tell "nothing is arriving" from "something arrives and we drop it".
  EXPECT_EQ(snapshot.panels[0].input_samples_received, 1u);
  EXPECT_GE(snapshot.panels[0].input_events_delivered, 2u);
  EXPECT_EQ(snapshot.panels[0].input_events_dropped, 0u);

  // The coarse sink still sees exactly one Click, at the panel's centre pixel.
  ASSERT_EQ(sink.events().size(), 1u);
  EXPECT_EQ(sink.events()[0].kind, PanelPointerEvent::Kind::Click);
  EXPECT_EQ(sink.events()[0].pointer_id, 1u);
  EXPECT_EQ(sink.events()[0].widget_x, 100);
  EXPECT_EQ(sink.events()[0].widget_y, 60);
}

TEST(PanelStreamInput, PressDragOffThePanelReleaseIsNotAClick)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  const QarVector3 centre = panel_centre(manager, "p");
  EXPECT_TRUE(manager.handle_sample(click_at(PointerPhase::Started, centre)));
  EXPECT_TRUE(panel.button()->isDown());
  EXPECT_TRUE(manager.pointer_state().is_pressed(1));

  // 5 m to the side: far off a 0.16 m panel, yet still resolved onto it.
  const QarVector3 far_off{centre.x + 5.0f, centre.y, centre.z};
  EXPECT_TRUE(manager.handle_sample(click_at(PointerPhase::Updated, far_off)))
    << "a grabbed pointer is projected onto its panel wherever it is";
  EXPECT_TRUE(manager.handle_sample(click_at(PointerPhase::Ended, far_off)));

  EXPECT_EQ(panel.clicks(), 0);
  EXPECT_FALSE(panel.button()->isDown());
  EXPECT_FALSE(manager.pointer_state().is_pressed(1));
}

TEST(PanelStreamInput, APressedPointerStaysWithItsPanelEvenOverAnother)
{
  ButtonPanel a;
  ButtonPanel b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("a", a.widget(), options_at(0.0f)).ok());
  ASSERT_TRUE(manager.add_panel("b", b.widget(), options_at(1.0f)).ok());
  manager.tick();

  // Press on A's plane, then move to a point ON B's plane. Without the grab the
  // nearest-plane pick would choose B; with it, A keeps the pointer.
  const QarVector3 on_a = panel_centre(manager, "a", 0.0f);
  const QarVector3 on_b = panel_centre(manager, "b", 1.0f);
  manager.handle_sample(click_at(PointerPhase::Started, on_a));
  manager.handle_sample(click_at(PointerPhase::Updated, on_b));
  EXPECT_EQ(manager.pointer_state().grabbed_panel(1), "a");
  manager.handle_sample(click_at(PointerPhase::Ended, on_b));

  EXPECT_EQ(a.clicks(), 1) << "the release projects onto A's centre: a click on A";
  EXPECT_EQ(b.clicks(), 0);
  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.panels[1].input_events_delivered, 0u) << "B saw nothing at all";
}

TEST(PanelStreamInput, TheQueuedSourceIsDrainedOnTick)
{
  ButtonPanel panel;
  auto source = std::make_shared<QueuedPanelInputSource>();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.input_source = source;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  source->push(click_at(PointerPhase::Instant, panel_centre(manager, "p")));
  EXPECT_EQ(panel.clicks(), 0) << "nothing happens until the GUI thread pumps";
  manager.tick();
  EXPECT_EQ(panel.clicks(), 1);
  EXPECT_EQ(source->pending(), 0u);
  EXPECT_EQ(manager.diagnostics().input_samples, 1u);

  source->push(click_at(PointerPhase::Instant, panel_centre(manager, "p"), 1, 5.0));
  EXPECT_EQ(manager.pump_input(), 1u);
  EXPECT_EQ(panel.clicks(), 2);
}

TEST(PanelStreamInput, RemovingAPanelCancelsThePointerPressedOnIt)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  manager.handle_sample(click_at(PointerPhase::Started, panel_centre(manager, "p")));
  ASSERT_TRUE(panel.button()->isDown());

  ASSERT_TRUE(manager.remove_panel("p").ok());
  EXPECT_FALSE(panel.button()->isDown()) << "the widget must not be left holding a press";
  EXPECT_FALSE(manager.pointer_state().is_pressed(1));
  EXPECT_EQ(panel.clicks(), 0);
}

TEST(PanelStreamInput, AlreadyResolvedPanelCoordinatesAreAcceptedDirectly)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  // The per-panel API: a key and (u, v), no room point.
  PanelPointerSample sample;
  sample.pointer_id = 3;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = "p";
  sample.u = 0.5f;
  sample.v = 0.5f;
  EXPECT_TRUE(manager.handle_sample(sample));
  EXPECT_EQ(panel.clicks(), 1);

  sample.u = 1.5f;  // outside the panel
  EXPECT_FALSE(manager.handle_sample(sample));
  EXPECT_EQ(panel.clicks(), 1);
  EXPECT_EQ(manager.diagnostics().input_samples_missed, 1u);
}

TEST(PanelStreamInput, InputCanBeDisabledPerPanel)
{
  ButtonPanel panel;
  PanelStreamOptions options = options_at();
  options.input.enabled = false;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options).ok());
  manager.tick();

  EXPECT_TRUE(manager.handle_sample(click_at(PointerPhase::Instant, panel_centre(manager, "p"))))
    << "the sample still resolves onto the panel";
  EXPECT_EQ(panel.clicks(), 0);
  const auto snapshot = manager.diagnostics();
  EXPECT_GE(snapshot.panels[0].input_events_dropped, 1u);
  EXPECT_NE(snapshot.panels[0].last_input_drop_reason.find("disabled"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Why the AR input pump must not run inside the host's timer handler.
//
// An operator who taps "Add" on a streamed RViz Displays panel from AR gets
// QDialog::exec() as RViz's answer, and the panel's heartbeat stops with ZERO
// render ticks — no capture, no submit, a frozen headset — until the dialog is
// dismissed from the desktop.
//
// The mechanism is Qt's, and the first test below pins it: a timer whose own
// handler enters a nested event loop is not re-entered while that loop runs
// (QEventDispatcherWin32's WinTimerInfo::inTimerEvent, and the equivalent guard
// on the other dispatchers). When the AR click is delivered from inside the
// render tick, the dialog's nested loop runs inside that timer's handler and
// kills it. Enter the same loop from a POSTED event and the timer is not on the
// stack, so it keeps firing — which is the fix, pinned by the second test and
// wired up by the RViz plugin's QarStreamingPanel::scheduleArPanelInputPump().
//
// The nested loops here are bounded by a singleShot quit, so nothing can hang
// the runner. QDialog::exec() itself is never called: it returns only when a
// dialog is dismissed, and nothing headless can dismiss it.
// ---------------------------------------------------------------------------

namespace
{

/// Pump the real event loop for @p ms without blocking it.
void
pump_for(int ms)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

/// Spin a nested QEventLoop for @p ms, exactly as QDialog::exec() does.
void
run_nested_loop_for(int ms)
{
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

/// A resolved click on the centre of panel @p key.
PanelPointerSample
centre_click(const std::string & key)
{
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = key;
  sample.u = 0.5f;
  sample.v = 0.5f;
  return sample;
}

}  // namespace

TEST(NestedEventLoop, ATimerWhoseHandlerEntersALoopIsNotReEnteredMeanwhile)
{
  int ticks = 0;
  int ticks_inside = -1;
  QTimer timer;
  timer.setInterval(5);
  timer.setTimerType(Qt::PreciseTimer);
  QObject::connect(
    &timer, &QTimer::timeout, [&ticks, &ticks_inside]() {
      ++ticks;
      if (ticks_inside >= 0) {
        return;  // only the first tick opens the "dialog"
      }
      const int before = ticks;
      run_nested_loop_for(150);
      ticks_inside = ticks - before;
    });
  timer.start();
  pump_for(400);
  timer.stop();

  ASSERT_GE(ticks, 1) << "the timer never fired at all";
  EXPECT_EQ(ticks_inside, 0)
    << "Qt re-entered a timer whose handler was still on the stack; the whole "
       "reason the AR input pump is posted instead of called inline is that it "
       "does not";
}

TEST(NestedEventLoop, ATimerKeepsFiringWhenTheLoopIsEnteredFromAPostedEvent)
{
  int ticks = 0;
  int ticks_inside = -1;
  QTimer timer;
  timer.setInterval(5);
  timer.setTimerType(Qt::PreciseTimer);
  QObject::connect(&timer, &QTimer::timeout, [&ticks]() { ++ticks; });
  timer.start();

  // The shape the RViz plugin's scheduleArPanelInputPump() gives its panel: the
  // work that may spin a nested loop is POSTED, so the timer handler has
  // already returned by the time the loop starts.
  QMetaObject::invokeMethod(
    QCoreApplication::instance(),
    [&ticks, &ticks_inside]() {
      const int before = ticks;
      run_nested_loop_for(150);
      ticks_inside = ticks - before;
    },
    Qt::QueuedConnection);
  pump_for(400);
  timer.stop();

  EXPECT_GT(ticks_inside, 0)
    << "the stream must keep capturing and submitting while a modal dialog is open";
}

TEST(PanelStreamInput, TickDoesNotDrainTheSourceWhenTheHostOwnsThePump)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto source = std::make_shared<QueuedPanelInputSource>();
  QtPanelStreamingConfig config = make_config(factory);
  config.input_source = source;
  // What the RViz plugin's QarStreamingPanel sets, because its tick IS a QTimer
  // handler.
  config.pump_input_in_tick = false;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  source->push(centre_click("p"));
  manager.tick();
  EXPECT_EQ(panel.clicks(), 0) << "tick() must leave the queue alone when opted out";
  EXPECT_EQ(source->pending(), 1u);

  EXPECT_EQ(manager.pump_input(), 1u);
  EXPECT_EQ(panel.clicks(), 1);
}

TEST(PanelStreamInput, TickStillDrainsTheSourceByDefault)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto source = std::make_shared<QueuedPanelInputSource>();
  QtPanelStreamingConfig config = make_config(factory);
  config.input_source = source;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  source->push(centre_click("p"));
  manager.tick();
  EXPECT_EQ(panel.clicks(), 1);
}

TEST(PanelStreamInput, PumpInputSurvivesBeingReEnteredByANestedLoop)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto source = std::make_shared<QueuedPanelInputSource>();
  QtPanelStreamingConfig config = make_config(factory);
  config.input_source = source;
  config.pump_input_in_tick = false;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  // The first click re-enters pump_input() from its own handler — precisely
  // what a modal dialog's nested loop does when the posted pump runs inside it.
  // The outer frame's reused scratch buffers must survive that.
  int reentries = 0;
  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      if (reentries++ > 0) {
        return;
      }
      source->push(centre_click("p"));
      manager.pump_input();
    });

  source->push(centre_click("p"));
  EXPECT_EQ(manager.pump_input(), 1u);

  EXPECT_EQ(panel.clicks(), 2) << "both the outer and the nested click landed";
  EXPECT_EQ(reentries, 2);
}

TEST(PanelStreamInput, DismissOwnedDialogsIsTheEscapeHatch)
{
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  EXPECT_EQ(manager.dismiss_owned_dialogs("p"), 0u);
  EXPECT_EQ(manager.dismiss_owned_dialogs("nobody"), 0u);

  QPointer<QDialog> dialog(new QDialog(panel.widget()));
  dialog->setModal(true);
  dialog->resize(120, 90);
  dialog->show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(dialog->isVisible());

  EXPECT_EQ(manager.dismiss_owned_dialogs("p"), 1u);
  QCoreApplication::processEvents();
  ASSERT_FALSE(dialog.isNull());
  EXPECT_FALSE(dialog->isVisible()) << "close() is what its own Cancel would do";
  delete dialog.data();
  QCoreApplication::processEvents();
}

// ---------------------------------------------------------------------------
// A dialog gets a GUI PANEL OF ITS OWN.
//
// The Displays dock is tall and narrow, the Add box is not, so compositing the
// dialog into the dock that opened it would show only the top half of the
// dialog and could never be right. It is streamed as a second panel instead,
// sized from the dialog.
//
// No exec() anywhere: setModal(true) + show() reaches the same modality state
// without a nested loop the test runner could not leave.
// ---------------------------------------------------------------------------

namespace
{

using qar_qt_streaming::PanelOverlayAttribution;

/// A dialog the manager's sweep can find, at a size nothing like its owner's.
/// Parented to the OWNER widget, so the parent walk alone attributes it and the
/// test does not have to stage an AR activation as well.
QDialog *
show_dialog(QWidget * parent, const QSize & size, const QString & title)
{
  auto * dialog = new QDialog(parent);
  dialog->setWindowTitle(title);
  dialog->resize(size);
  dialog->show();
  dialog->ensurePolished();
  QCoreApplication::processEvents();
  return dialog;
}

/// The adapter created for @p key, or null. The factory keeps every adapter it ever
/// handed out, which is what makes "was this one closed" answerable.
std::shared_ptr<qar_qt_streaming::test_support::MockQarPanelSenderAdapter>
adapter_for(const MockQarPanelSenderFactory & factory, const std::string & key)
{
  for (const auto & adapter : factory.created()) {
    if (adapter->desc().key == key) {
      return adapter;
    }
  }
  return nullptr;
}

/// A manager fixture that leaves the process-wide attribution state clean.
struct DialogPanelFixture
{
  DialogPanelFixture()
  {
    PanelOverlayAttribution::instance().reset();
  }
  ~DialogPanelFixture()
  {
    PanelOverlayAttribution::instance().reset();
  }
};

}  // namespace

TEST(DialogPanels, ADialogBecomesASecondPanelSizedFromItself)
{
  DialogPanelFixture guard;
  ButtonPanel panel;  // 200 x 120
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("dock:Displays", panel.widget(), options_at()).ok());
  manager.tick();
  ASSERT_EQ(manager.panel_keys().size(), 1u);

  // Wide and short where the owner is narrow and tall: if the dialog were still
  // being squeezed into the owner's rectangle this is the assertion that fails.
  QDialog * dialog = show_dialog(panel.widget(), QSize(360, 90), "Add");
  const QSize dialog_size = dialog->size();
  const QPoint dialog_pos = dialog->pos();

  manager.tick();

  const std::vector<std::string> dialog_keys = manager.dialog_panel_keys();
  ASSERT_EQ(dialog_keys.size(), 1u);
  EXPECT_EQ(dialog_keys.front(), "dialog:Add") << "stable and readable in the status line";
  EXPECT_EQ(manager.panel_keys().size(), 2u);
  EXPECT_TRUE(manager.has_panel("dialog:Add"));

  const auto adapter = adapter_for(*factory, "dialog:Add");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->desc().display_name, "Add");
  EXPECT_EQ(adapter->desc().buffer_width, static_cast<std::uint32_t>(dialog_size.width()));
  EXPECT_EQ(adapter->desc().buffer_height, static_cast<std::uint32_t>(dialog_size.height()));

  // Beside its owner, not on top of it, and the operator's real window is left
  // exactly where it was — the panel never rearranges it.
  const auto owner = adapter_for(*factory, "dock:Displays");
  ASSERT_NE(owner, nullptr);
  EXPECT_FLOAT_EQ(
    adapter->desc().placement.pose.position.x,
    owner->desc().placement.pose.position.x + 0.5f);
  EXPECT_FLOAT_EQ(
    adapter->desc().placement.pose.position.y, owner->desc().placement.pose.position.y);
  EXPECT_EQ(dialog->pos(), dialog_pos);
  EXPECT_EQ(dialog->size(), dialog_size);

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, ClosingTheDialogClosesItsPanelAndDestroysItsSender)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));  // the default mode: destroy
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_EQ(manager.dialog_panel_keys().size(), 1u);
  const auto adapter = adapter_for(*factory, "dialog:Add");
  ASSERT_NE(adapter, nullptr);
  ASSERT_EQ(adapter->close_calls(), 0u);
  const auto owner = adapter_for(*factory, "p");
  ASSERT_NE(owner, nullptr);

  // close() is what its own Cancel does, and what exec() returning does: the
  // dialog is hidden, not destroyed. Hidden is enough to retire the panel.
  dialog->close();
  QCoreApplication::processEvents();
  manager.tick();

  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  // The dialog's AR panel is CLOSED and its video sender DESTROYED — exactly
  // once, in the contract's order (input, then the panel, then the sender) — and
  // the stream is forgotten: no notice, no slot kept.
  EXPECT_FALSE(manager.has_panel("dialog:Add"));
  EXPECT_FALSE(manager.is_tombstoned("dialog:Add"));
  EXPECT_EQ(manager.panel_keys(), std::vector<std::string>{"p"});
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(
    adapter->close_steps(),
    (std::vector<std::string>{"unsubscribe_input", "close_panel", "destroy_sender"}));
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  // The owner is untouched.
  EXPECT_EQ(owner->close_calls(), 0u);
  EXPECT_TRUE(manager.has_panel("p"));
  EXPECT_EQ(manager.diagnostics().panels_closed, 1u);

  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, ClosingTheDialogInNoticeModeTombstonesItsPanelAndKeepsItsAdapter)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_EQ(manager.dialog_panel_keys().size(), 1u);
  const auto adapter = adapter_for(*factory, "dialog:Add");
  ASSERT_NE(adapter, nullptr);
  ASSERT_EQ(adapter->close_calls(), 0u);

  dialog->close();
  QCoreApplication::processEvents();
  manager.tick();

  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  // Notice mode: the PANEL stays, as a notice. Destroying its video sender here
  // can freeze the 3D scene on a runtime that does not survive it (see
  // panel_tombstone.hpp), so the sender is kept until the session ends.
  EXPECT_TRUE(manager.has_panel("dialog:Add"));
  EXPECT_TRUE(manager.is_tombstoned("dialog:Add"));
  EXPECT_EQ(manager.panel_keys().size(), 2u);
  EXPECT_EQ(adapter->close_calls(), 0u);

  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, ADestroyedDialogClosesItsPanelBeforeAnythingPaints)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_TRUE(manager.has_panel("dialog:Add"));
  const auto adapter = adapter_for(*factory, "dialog:Add");
  ASSERT_NE(adapter, nullptr);

  // Deleted outright, the way RViz disposes of a one-shot dialog. The sweep runs
  // before any capture, so nothing ever paints through the dangling pointer.
  delete dialog;
  QCoreApplication::processEvents();
  manager.tick();

  // Closed and gone — and crucially never captured through the dangling pointer,
  // which is what the sweep running before any paint buys.
  EXPECT_FALSE(manager.has_panel("dialog:Add"));
  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  // Nothing is left to stream; ticking again must not crash or fault.
  manager.tick();
  EXPECT_FALSE(manager.has_panel("dialog:Add"));
  EXPECT_EQ(adapter->close_calls(), 1u);
}

TEST(DialogPanels, ADestroyedDialogInNoticeModeTombstonesItsPanelBeforeAnythingPaints)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_TRUE(manager.has_panel("dialog:Add"));

  delete dialog;
  QCoreApplication::processEvents();
  manager.tick();

  // Still a panel, now a notice — and crucially never captured through the
  // dangling pointer, which is what the sweep running before any paint buys.
  EXPECT_TRUE(manager.is_tombstoned("dialog:Add"));
  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  // A tombstoned panel goes on streaming its notice without touching the dead
  // widget; ticking again must not crash or fault.
  manager.tick();
  EXPECT_TRUE(manager.is_tombstoned("dialog:Add"));
}

TEST(DialogPanels, RepeatedOpenAndCloseLeaksNothing)
{
  // The pointer-slot arena behind the real input source is process-lifetime and
  // never reused, so one leaked panel per dialog opened would eventually exhaust
  // it. Closing destroys: each showing gets a panel and a sender of its own, and
  // each is closed and destroyed exactly once when the dialog goes. Nothing is
  // kept, so nothing accumulates however often the dialog is opened.
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();
  const std::uint32_t after_owner = factory->create_calls();

  for (int round = 0; round < 4; ++round) {
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    manager.tick();
    ASSERT_EQ(manager.dialog_panel_keys().size(), 1u) << "round " << round;
    EXPECT_EQ(manager.dialog_panel_keys().front(), "dialog:Add")
      << "the key is free again after a close, so the name never grows a suffix";
    // Idle ticks while it is open must not mint a second panel for it.
    manager.tick();
    manager.tick();
    EXPECT_EQ(manager.dialog_panel_keys().size(), 1u) << "round " << round;

    dialog->hide();
    QCoreApplication::processEvents();
    manager.tick();
    EXPECT_TRUE(manager.dialog_panel_keys().empty()) << "round " << round;
    EXPECT_EQ(manager.panel_keys(), std::vector<std::string>{"p"}) << "round " << round;
    delete dialog;
    QCoreApplication::processEvents();
  }

  EXPECT_EQ(factory->create_calls(), after_owner + 4u)
    << "every showing is a fresh add: a new GUI panel on a new sender";
  std::uint32_t dialog_senders = 0;
  for (const auto & adapter : factory->created()) {
    if (adapter->desc().key.rfind("dialog:", 0) == 0) {
      EXPECT_EQ(adapter->close_calls(), 1u) << "each dialog sender is destroyed once, when it closes";
      EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
      ++dialog_senders;
    }
  }
  EXPECT_EQ(dialog_senders, 4u);
  // Only the owner is left, however many times the dialog was opened.
  EXPECT_EQ(manager.panel_keys().size(), 1u);
  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.panels_closed, 4u);
  EXPECT_EQ(snapshot.tombstoned_panels, 0u);
  EXPECT_EQ(snapshot.tombstones_revived, 0u);
  // The owner's sender was never touched.
  EXPECT_EQ(adapter_for(*factory, "p")->close_calls(), 0u);
}

TEST(DialogPanels, RepeatedOpenAndCloseInNoticeModeReusesTheOneSlot)
{
  // The escape hatch's bound: a closed dialog's sender is NOT destroyed, so the
  // dialog's key is reusable while tombstoned, and re-opening the same dialog
  // revives that one slot. Four showings, ONE sender, none of them closed.
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();
  const std::uint32_t after_owner = factory->create_calls();

  for (int round = 0; round < 4; ++round) {
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    manager.tick();
    ASSERT_EQ(manager.dialog_panel_keys().size(), 1u) << "round " << round;
    // Idle ticks while it is open must not mint a second panel for it.
    manager.tick();
    manager.tick();
    EXPECT_EQ(manager.dialog_panel_keys().size(), 1u) << "round " << round;

    dialog->hide();
    QCoreApplication::processEvents();
    manager.tick();
    EXPECT_TRUE(manager.dialog_panel_keys().empty()) << "round " << round;
    delete dialog;
    QCoreApplication::processEvents();
  }

  EXPECT_EQ(factory->create_calls(), after_owner + 1u)
    << "one sender for the dialog across all four showings — the retired slot is re-used";
  std::uint32_t dialog_senders = 0;
  for (const auto & adapter : factory->created()) {
    if (adapter->desc().key.rfind("dialog:", 0) == 0) {
      EXPECT_EQ(adapter->close_calls(), 0u) << "no sender may be destroyed mid-session";
      ++dialog_senders;
    }
  }
  EXPECT_EQ(dialog_senders, 1u);
  // The owner plus exactly one dialog slot, however many times it was opened.
  EXPECT_EQ(manager.panel_keys().size(), 2u);
  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.tombstoned_panels, 1u);
  EXPECT_EQ(snapshot.tombstones_revived, 3u);
}

TEST(DialogPanels, ADialogPanelIsClosedWithItsOwner)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_TRUE(manager.has_panel("dialog:Add"));

  // The operator stops streaming the dock. Its dialog was only ever an extension
  // of that session, and an orphan would otherwise stream forever.
  ASSERT_TRUE(manager.remove_panel("p").ok());
  EXPECT_FALSE(manager.has_panel("p"));
  manager.tick();

  // Both panels are closed and both senders destroyed, once each. The dialog
  // panel stops being a DIALOG panel the moment its owner stops streaming, which
  // is what keeps the sweep from ticking an orphan.
  EXPECT_FALSE(manager.has_panel("dialog:Add"));
  EXPECT_TRUE(manager.panel_keys().empty());
  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  ASSERT_EQ(factory->created().size(), 2u);
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u) << adapter->desc().key;
    EXPECT_EQ(adapter->sender_destroy_calls(), 1u) << adapter->desc().key;
  }
  // The dialog went second: the owner's own close came first, then the sweep.
  EXPECT_EQ(
    factory->events(),
    (std::vector<std::string>{"create p", "create dialog:Add", "close p", "close dialog:Add"}));

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, ADialogPanelIsTombstonedWithItsOwnerInNoticeMode)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  ASSERT_TRUE(manager.has_panel("dialog:Add"));

  // The operator stops streaming the dock. Its dialog was only ever an extension
  // of that session, and an orphan would otherwise stream forever.
  ASSERT_TRUE(manager.remove_panel("p").ok());
  manager.tick();

  // Both become notices; neither sender is destroyed. The dialog panel stops
  // being a DIALOG panel (its owner stopped streaming), which is what keeps the
  // sweep from ticking an orphan.
  EXPECT_TRUE(manager.is_tombstoned("p"));
  EXPECT_TRUE(manager.is_tombstoned("dialog:Add"));
  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u);
  }

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, AnUnattributedDialogCreatesNothing)
{
  // Opened with the local desktop mouse, from a window nobody is streaming: no
  // panel caused it, so no panel adopts it. Invisible in AR, answerable on the
  // desktop.
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  auto * stranger_window = new QWidget();
  stranger_window->resize(300, 200);
  stranger_window->show();
  QCoreApplication::processEvents();
  QDialog * dialog = show_dialog(stranger_window, QSize(240, 100), "Stranger");

  manager.tick();
  manager.tick();

  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  EXPECT_EQ(manager.panel_keys().size(), 1u);
  EXPECT_FALSE(manager.has_panel("dialog:Stranger"));

  dialog->hide();
  QCoreApplication::processEvents();
  delete stranger_window;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, TwoDialogsFromOnePanelGetDistinctKeysAndDistinctPlaces)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * first = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();
  QDialog * second = show_dialog(panel.widget(), QSize(200, 140), "Add");
  manager.tick();

  const std::vector<std::string> keys = manager.dialog_panel_keys();
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], "dialog:Add");
  EXPECT_EQ(keys[1], "dialog:Add #2") << "two boxes can share a title; a key may not";

  const auto a = adapter_for(*factory, "dialog:Add");
  const auto b = adapter_for(*factory, "dialog:Add #2");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_FLOAT_EQ(b->desc().placement.pose.position.x, a->desc().placement.pose.position.x + 0.5f)
    << "stepped sideways again, so they do not stack on each other";
  EXPECT_EQ(b->desc().buffer_width, 200u);
  EXPECT_EQ(b->desc().buffer_height, 140u);

  for (QDialog * dialog : {first, second}) {
    dialog->hide();
    QCoreApplication::processEvents();
    delete dialog;
    QCoreApplication::processEvents();
  }
}

TEST(DialogPanels, ARClicksReachTheDialogThroughItsOwnPanel)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory));
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  auto * dialog = new QDialog(panel.widget());
  dialog->setWindowTitle("Add");
  auto * button = new QPushButton("OK", dialog);
  button->setGeometry(0, 0, 240, 100);
  int dialog_clicks = 0;
  QObject::connect(button, &QPushButton::clicked, [&dialog_clicks]() { ++dialog_clicks; });
  dialog->resize(240, 100);
  dialog->setModal(true);
  dialog->show();
  QCoreApplication::processEvents();

  manager.tick();
  ASSERT_TRUE(manager.has_panel("dialog:Add"));

  // The ordinary panel input path, addressed to the dialog's own panel by key.
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = "dialog:Add";
  sample.u = 0.5f;
  sample.v = 0.5f;
  EXPECT_TRUE(manager.handle_sample(sample));
  EXPECT_EQ(dialog_clicks, 1) << "the modal rule locked the operator out of its own dialog";

  // ...while the owner panel is modal-blocked, as Qt blocks the window behind.
  const int owner_clicks_before = panel.clicks();
  PanelPointerSample at_owner = sample;
  at_owner.panel_key = "p";
  manager.handle_sample(at_owner);
  EXPECT_EQ(panel.clicks(), owner_clicks_before);

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogPanels, PromotionCanBeTurnedOff)
{
  DialogPanelFixture guard;
  ButtonPanel panel;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory);
  config.dialog_panels = false;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", panel.widget(), options_at()).ok());
  manager.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  manager.tick();

  EXPECT_TRUE(manager.dialog_panel_keys().empty());
  EXPECT_EQ(manager.panel_keys().size(), 1u) << "opted out: the dialog is invisible in AR (it gets no panel)";

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}
