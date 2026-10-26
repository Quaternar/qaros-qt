// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Hiding a streamed panel in AR without closing it: QtPanelStreamManager::set_panel_ar_visible
// records the request, the next tick carries it out through IQarPanelSenderAdapter::set_state,
// and a hidden panel is neither captured nor pointed at. MockQarPanelSenderAdapter records the
// state calls and can refuse them, so what is asserted here is what the library owes: the
// request is applied on the next tick and not before, the old state survives a refusal, input
// and hit tests skip a hidden panel, and a close still closes everything once. What the real
// qar_gui_panels_set_state does to a panel needs the SDK and is checked by running against it.
// A hide request also lets go of every pointer pressed on the panel, there and then, because a
// hidden panel takes no input and the widget would otherwise stay pressed.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelPointerEvent;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::PanelPointerSample;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_ar_core::QueuedPanelInputSource;
using qar_ar_core::Status;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderAdapter;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordingPanelInputSink;

namespace
{

/// A streamable widget: a solid colour block, shown under the offscreen platform plugin so
/// render() draws its children.
class SourceWidget
{
public:
  explicit SourceWidget(int w = 200, int h = 120)
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    auto * block = new QWidget(widget_);
    block->setAutoFillBackground(true);
    QPalette palette = block->palette();
    palette.setColor(QPalette::Window, Qt::blue);
    block->setPalette(palette);
    layout->addWidget(block);
    widget_->resize(w, h);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~SourceWidget() { delete widget_; }

  QWidget * get() const { return widget_; }

private:
  QWidget * widget_ = nullptr;
};

/// A 200 x 120 panel whose whole area is one QPushButton.
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
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~ButtonPanel() { delete widget_; }

  QWidget * widget() const { return widget_; }
  QPushButton * button() const { return button_; }

private:
  QWidget * widget_ = nullptr;
  QPushButton * button_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;              // every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  return options;
}

/// A manager wired to the recording factory, a manual clock and a recording sink, with dialog
/// promotion off and input pumped by the test (the way the RViz plugin runs it).
struct Harness
{
  explicit Harness(PanelCloseMode mode = PanelCloseMode::Destroy)
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    clock = std::make_shared<ManualClock>();
    queue = std::make_shared<QueuedPanelInputSource>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = false;
    config.input_source = queue;
    config.input_sink = &sink;
    config.pump_input_in_tick = false;
    config.close_mode = mode;
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  /// One tick at the next instant; the number of panels that submitted a frame.
  std::uint32_t tick()
  {
    clock->advance(0.1);
    return manager->tick();
  }

  /// The snapshot entry of @p key (a default one when there is none).
  qar_ar_core::PanelStreamDiagnostics panel(const std::string & key) const
  {
    for (const auto & entry : manager->diagnostics().panels) {
      if (entry.key == key) {
        return entry;
      }
    }
    return qar_ar_core::PanelStreamDiagnostics{};
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  std::shared_ptr<QueuedPanelInputSource> queue;
  RecordingPanelInputSink sink;
  std::unique_ptr<QtPanelStreamManager> manager;
};

/// A sender that knows nothing of panel state: it implements the pure virtuals of the interface
/// and no more, so its set_state() and supports_state() are the interface's own defaults.
class StatelessSender : public qar_ar_core::IQarPanelSenderAdapter
{
public:
  bool try_begin_frame(bool & out_ready) override
  {
    out_ready = true;
    return true;
  }
  bool submit_frame(const qar_ar_core::PanelPixels &) override { return true; }
  bool set_placement(const qar_ar_core::PanelPlacement &) override { return true; }
  using qar_ar_core::IQarPanelSenderAdapter::close;
  qar_ar_core::PanelCloseReport close(bool) override { return qar_ar_core::PanelCloseReport{}; }
  const std::string & last_error() const override { return error_; }

private:
  std::string error_;
};

class StatelessFactory : public qar_ar_core::IQarPanelSenderFactory
{
public:
  std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> create_panel_sender(
    const qar_ar_core::PanelDesc &) override
  {
    sender = std::make_shared<StatelessSender>();
    return sender;
  }
  const std::string & last_error() const override { return error_; }

  std::shared_ptr<StatelessSender> sender;

private:
  std::string error_;
};

PanelPointerSample
centre_sample(const std::string & key, PointerPhase phase)
{
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = phase;
  sample.has_panel_uv = true;
  sample.panel_key = key;
  sample.u = 0.5f;
  sample.v = 0.5f;
  return sample;
}

const std::vector<std::string> kFullClose = {"unsubscribe_input", "close_panel", "destroy_sender"};
const std::vector<std::string> kSkippedClose = {
  "unsubscribe_input", "close_panel_skipped", "destroy_sender"};

}  // namespace

TEST(QtPanelVisibility, HideAppliesOnNextTickAndPausesCapture)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_NE(adapter, nullptr);
  const std::uint32_t begins = adapter->begin_calls();
  const std::uint32_t submits = adapter->submit_calls();
  const std::uint64_t captured = h.panel("p").frames_captured;
  ASSERT_EQ(adapter->state_calls(), 0u);
  EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);

  // The call records the request and nothing else: no QarOS call, nothing changes yet.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  EXPECT_EQ(adapter->state_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_FALSE(h.panel("p").ar_hidden);

  // The next tick carries it out, first, and captures nothing.
  EXPECT_EQ(h.tick(), 0u);
  ASSERT_EQ(adapter->state_calls(), 1u);
  EXPECT_EQ(adapter->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_HIDDEN}));
  EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_TRUE(h.manager->is_ar_hidden("p"));
  qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_TRUE(d.ar_hidden);
  EXPECT_EQ(d.visibility_changes, 1u);
  EXPECT_EQ(d.visibility_failures, 0u);
  EXPECT_TRUE(d.last_visibility_error.empty());
  EXPECT_EQ(d.frames_skipped_hidden, 1u);

  // However many ticks follow, and whatever the widget does: no capture, no send cycle, no
  // frame; and nothing is closed or re-created.
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(h.tick(), 0u);
  }
  d = h.panel("p");
  EXPECT_EQ(d.frames_captured, captured);
  EXPECT_EQ(d.frames_skipped_hidden, 6u);
  EXPECT_EQ(adapter->begin_calls(), begins);
  EXPECT_EQ(adapter->submit_calls(), submits);
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_TRUE(h.manager->has_panel("p"));

  // Hiding what is hidden asks QarOS for nothing.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 1u);
  EXPECT_EQ(h.panel("p").visibility_changes, 1u);

  // The widget is resized while the panel is hidden. No tick measures it, so the sender is not
  // rebuilt - a rebuild would put a new QarOS panel on screen under a stream that believes it
  // is hidden - and the new size is adopted like any other once the panel is shown.
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  for (int i = 0; i < 6; ++i) {
    h.clock->advance(0.2);
    h.manager->tick();
  }
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(h.panel("p").adapter_rebuilds, 1u);
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  for (int i = 0; i < 6; ++i) {
    h.clock->advance(0.2);
    h.manager->tick();
  }
  EXPECT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(adapter->close_calls(), 1u) << "the old sender went before the new one was made";
  EXPECT_EQ(h.panel("p").adapter_rebuilds, 2u);
  EXPECT_EQ(h.panel("p").buffer_width, 400u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
}

TEST(QtPanelVisibility, ShowResumesAndSubmitsOnce)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  ASSERT_EQ(h.tick(), 0u);
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));
  const std::uint32_t submits = adapter->submit_calls();
  const std::uint64_t skipped = h.panel("p").frames_skipped_hidden;

  // Shown again: the tick that carries it out also captures, and sends a fresh frame although
  // the widget has not changed since the last one - the receiver has seen none for a while.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  EXPECT_EQ(adapter->state_calls(), 1u) << "still only a request";
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(adapter->states(), (std::vector<QarGuiPanelState>{
      QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_VISIBLE}));
  EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(adapter->submit_calls(), submits + 1u);
  qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_FALSE(d.ar_hidden);
  EXPECT_EQ(d.visibility_changes, 2u);
  EXPECT_EQ(d.frames_skipped_hidden, skipped) << "no tick is skipped once it is shown";

  // Once: an unchanged widget is not sent again.
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(adapter->submit_calls(), submits + 1u);

  // Showing what is shown asks QarOS for nothing.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 2u);
}

TEST(QtPanelVisibility, HiddenPanelIsNotHit)
{
  Harness h;
  SourceWidget front;
  SourceWidget behind;
  PanelStreamOptions behind_options = options_no_pacing();
  behind_options.placement.pose.position = QarVector3{0.0f, 0.0f, -0.5f};
  ASSERT_TRUE(h.manager->add_panel("front", front.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("behind", behind.get(), behind_options).ok());
  ASSERT_EQ(h.tick(), 2u);
  // Half the height of the panel, on its middle line: a point of the video, which is the part of
  // the panel under the title bar and left of the strip at the right.
  const float panel_height_m = h.panel("front").height_meters;
  ASSERT_GT(panel_height_m, static_cast<float>(qar_qt_streaming::kDefaultArFrameTopM));
  const QarVector3 origin{0.0f, -0.5f * panel_height_m, 1.0f};
  const QarVector3 direction{0.0f, 0.0f, -1.0f};

  // The ray meets the front panel first.
  ASSERT_TRUE(h.manager->route_room_ray(origin, direction, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(h.sink.events().size(), 1u);
  EXPECT_EQ(h.sink.events().back().panel_key, "front");

  // Hidden, the front panel is not there to be pointed at: the same ray lands on the panel
  // behind it, and a sample addressed to the hidden panel itself lands nowhere.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("front", false).ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("front"));
  ASSERT_TRUE(h.manager->route_room_ray(origin, direction, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(h.sink.events().size(), 2u);
  EXPECT_EQ(h.sink.events().back().panel_key, "behind");
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("front", PointerPhase::Instant)));
  EXPECT_EQ(h.sink.events().size(), 2u);

  // Shown again, it is the first thing the ray meets once more.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("front", true).ok());
  h.tick();
  ASSERT_TRUE(h.manager->route_room_ray(origin, direction, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(h.sink.events().size(), 3u);
  EXPECT_EQ(h.sink.events().back().panel_key, "front");
  EXPECT_TRUE(h.manager->handle_sample(centre_sample("front", PointerPhase::Instant)));
}

TEST(QtPanelVisibility, InputForHiddenPanelIsDropped)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  int clicks = 0;
  QObject::connect(panel.button(), &QPushButton::clicked, [&]() { ++clicks; });

  // The hide is asked for, and a press lands before the tick that carries it out - the panel
  // is still shown until then - so the press is held when the panel goes hidden.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  ASSERT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Started)));
  ASSERT_EQ(h.panel("p").input_events_dropped, 0u);
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));

  // The release finds the panel hidden: what the pointer state machine addresses to it is
  // dropped with the reason, counted, and never reaches the widget as a click.
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("p", PointerPhase::Ended)));
  const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_GE(d.input_events_dropped, 1u);
  EXPECT_EQ(d.last_input_drop_reason, "hidden in AR");
  EXPECT_EQ(clicks, 0);

  // And a tap aimed at the hidden panel lands nowhere.
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("p", PointerPhase::Instant)));
  EXPECT_EQ(clicks, 0);
}

TEST(QtPanelVisibility, HideReleasesAPressedPointer)
{
  Harness h;
  ButtonPanel panel;
  ButtonPanel other;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("q", other.widget(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);
  int clicks = 0;
  QObject::connect(panel.button(), &QPushButton::clicked, [&]() { ++clicks; });
  int other_clicks = 0;
  QObject::connect(other.button(), &QPushButton::clicked, [&]() { ++other_clicks; });

  // One pointer is held on each panel.
  PanelPointerSample press_other = centre_sample("q", PointerPhase::Started);
  press_other.pointer_id = 2;
  ASSERT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Started)));
  ASSERT_TRUE(h.manager->handle_sample(press_other));
  ASSERT_TRUE(panel.button()->isDown());
  ASSERT_TRUE(other.button()->isDown());

  // Asking for what is already so lets go of nothing.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  EXPECT_TRUE(panel.button()->isDown());

  // The hide is asked for: the pointer pressed on that panel is let go of there and then,
  // before any tick, as a cancel and not as a click. The pointer held on the other panel is
  // not touched, and the hide itself still waits for the tick.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  EXPECT_FALSE(panel.button()->isDown());
  EXPECT_EQ(clicks, 0);
  EXPECT_TRUE(other.button()->isDown());
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(h.panel("p").input_events_dropped, 0u) << "the release was delivered, not dropped";

  // The tick hides the panel. The release that follows finds no press to end and reaches no
  // widget, and nothing is counted as dropped for the hidden panel.
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("p", PointerPhase::Ended)));
  EXPECT_EQ(clicks, 0);
  EXPECT_EQ(h.panel("p").input_events_dropped, 0u);

  // The pointer held on the other panel ends with its click as usual.
  PanelPointerSample release_other = centre_sample("q", PointerPhase::Ended);
  release_other.pointer_id = 2;
  EXPECT_TRUE(h.manager->handle_sample(release_other));
  EXPECT_FALSE(other.button()->isDown());
  EXPECT_EQ(other_clicks, 1);

  // Shown again, the panel takes input as before: a fresh press and release click once.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  ASSERT_FALSE(h.manager->is_ar_hidden("p"));
  ASSERT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Started)));
  EXPECT_TRUE(panel.button()->isDown());
  EXPECT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Ended)));
  EXPECT_FALSE(panel.button()->isDown());
  EXPECT_EQ(clicks, 1);
}

TEST(QtPanelVisibility, UnsupportedBackendFailsAndCounts)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  adapter->set_supports_state(false);
  const std::uint64_t captured = h.panel("p").frames_captured;

  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();

  // The stream does not even ask a backend that says it cannot: the old state stays, the
  // failure is counted and says why, and the panel goes on being captured.
  EXPECT_EQ(adapter->state_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_FALSE(d.ar_hidden);
  EXPECT_EQ(d.visibility_changes, 0u);
  EXPECT_EQ(d.visibility_failures, 1u);
  EXPECT_NE(d.last_visibility_error.find("cannot change the state"), std::string::npos)
    << d.last_visibility_error;
  EXPECT_EQ(d.frames_skipped_hidden, 0u);
  EXPECT_EQ(d.frames_captured, captured + 1u);

  // The request was used up: a later tick does not count it again.
  h.tick();
  EXPECT_EQ(h.panel("p").visibility_failures, 1u);
  EXPECT_EQ(adapter->close_calls(), 0u) << "falling back to a close is the caller's call";

  // A sender written before panel state existed has only the interface's defaults, which
  // refuse. The answer is the same, without any help from the recording adapter.
  auto stateless = std::make_shared<StatelessFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = stateless;
  config.clock = clock;
  config.dialog_panels = false;
  QtPanelStreamManager manager(config);
  SourceWidget other;
  ASSERT_TRUE(manager.add_panel("q", other.get(), options_no_pacing()).ok());
  clock->advance(0.1);
  ASSERT_EQ(manager.tick(), 1u);
  ASSERT_NE(stateless->sender, nullptr);
  EXPECT_FALSE(stateless->sender->supports_state());
  EXPECT_FALSE(stateless->sender->set_state(QAR_GUI_PANEL_STATE_HIDDEN));
  ASSERT_TRUE(manager.set_panel_ar_visible("q", false).ok());
  clock->advance(0.1);
  manager.tick();
  EXPECT_FALSE(manager.is_ar_hidden("q"));
  ASSERT_EQ(manager.diagnostics().panels.size(), 1u);
  EXPECT_EQ(manager.diagnostics().panels.front().visibility_failures, 1u);
  EXPECT_NE(manager.diagnostics().panels.front().last_visibility_error.find(
      "cannot change the state"), std::string::npos);
}

TEST(QtPanelVisibility, FailedSetStateKeepsStateAndCounts)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  const std::uint64_t captured = h.panel("p").frames_captured;

  // A refused hide: asked once, the panel stays shown and keeps being captured.
  adapter->fail_next_set_state();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 1u);
  EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_EQ(d.visibility_changes, 0u);
  EXPECT_EQ(d.visibility_failures, 1u);
  EXPECT_EQ(d.last_visibility_error, "injected set_state failure");
  EXPECT_EQ(d.frames_captured, captured + 1u);
  EXPECT_EQ(d.frames_skipped_hidden, 0u);

  // The next hide works, and the reason is gone with the failure it explained.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();
  EXPECT_TRUE(h.manager->is_ar_hidden("p"));
  d = h.panel("p");
  EXPECT_EQ(d.visibility_changes, 1u);
  EXPECT_EQ(d.visibility_failures, 1u);
  EXPECT_TRUE(d.last_visibility_error.empty());

  // A refused show: the panel stays hidden and keeps sitting its ticks out.
  adapter->fail_next_set_state();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  EXPECT_TRUE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(adapter->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  d = h.panel("p");
  EXPECT_EQ(d.visibility_changes, 1u);
  EXPECT_EQ(d.visibility_failures, 2u);
  EXPECT_EQ(d.last_visibility_error, "injected set_state failure");
  EXPECT_GE(d.frames_skipped_hidden, 1u);
}

TEST(QtPanelVisibility, RequestBeforeAdapterIsAppliedAfterCreation)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());

  // Asked before the first tick, so before there is any QarOS panel to ask about.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  EXPECT_EQ(h.factory->create_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));

  // The tick that creates the sender carries the request out right after, and captures and
  // sends nothing: the panel is born hidden.
  EXPECT_EQ(h.tick(), 0u);
  ASSERT_EQ(h.factory->create_calls(), 1u);
  const auto adapter = h.factory->latest();
  EXPECT_EQ(adapter->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_HIDDEN}));
  EXPECT_TRUE(h.manager->is_ar_hidden("p"));
  const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
  EXPECT_TRUE(d.active);
  EXPECT_EQ(d.visibility_changes, 1u);
  EXPECT_EQ(d.frames_captured, 0u);
  EXPECT_EQ(d.frames_submitted, 0u);
  EXPECT_EQ(d.frames_skipped_hidden, 1u);
  EXPECT_EQ(adapter->submit_calls(), 0u);

  // Shown later, it sends its first frame.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(adapter->submit_calls(), 1u);
}

TEST(QtPanelVisibility, CloseOfHiddenPanelClosesOnce)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));
  ASSERT_EQ(adapter->close_calls(), 0u) << "hiding closes nothing";

  // The ordinary close: the QarOS panel and the sender go once, in the contract's order, and
  // the stream is forgotten.
  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(adapter->close_steps(), kFullClose);
  EXPECT_EQ(adapter->close_panel_calls(), 1u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  EXPECT_FALSE(adapter->last_close_panel_closed_in_ar());
  EXPECT_FALSE(h.manager->has_panel("p"));
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.panels_closed, 1u);
  EXPECT_EQ(snapshot.hidden_panels, 0u);

  // The session ending after it finds nothing left to close.
  h.manager->clear();
  EXPECT_EQ(adapter->close_calls(), 1u);
}

TEST(QtPanelVisibility, ShowOfClosedInArPanelResumesWhenAccepted)
{
  // The operator closed the panel in AR; capture stopped and the sender stays. Showing is
  // tried, and when QarOS accepts it the panel is live again.
  {
    Harness h;
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    const auto adapter = h.factory->latest();
    ASSERT_TRUE(h.manager->note_ar_panel_closed("p"));
    EXPECT_EQ(h.tick(), 0u);
    ASSERT_TRUE(h.panel("p").ar_panel_closed);
    const std::uint32_t submits = adapter->submit_calls();

    ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
    EXPECT_EQ(h.tick(), 1u) << "capture resumes on the tick that brings the panel back";
    EXPECT_EQ(adapter->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_VISIBLE}));
    const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
    EXPECT_FALSE(d.ar_panel_closed);
    EXPECT_FALSE(d.ar_hidden);
    EXPECT_EQ(d.visibility_changes, 1u);
    EXPECT_EQ(adapter->submit_calls(), submits + 1u);

    // It is a live panel again, so closing it closes the QarOS panel too - once.
    ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
    EXPECT_EQ(adapter->close_steps(), kFullClose);
    EXPECT_FALSE(adapter->last_close_panel_closed_in_ar());
  }

  // Refused, it stays what it was: closed in AR, not captured, not closed a second time.
  {
    Harness h;
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    const auto adapter = h.factory->latest();
    ASSERT_TRUE(h.manager->note_ar_panel_closed("p"));
    const std::uint32_t submits = adapter->submit_calls();

    adapter->fail_next_set_state();
    ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
    EXPECT_EQ(h.tick(), 0u);
    const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
    EXPECT_TRUE(d.ar_panel_closed);
    EXPECT_EQ(d.visibility_failures, 1u);
    EXPECT_EQ(d.visibility_changes, 0u);
    EXPECT_EQ(adapter->submit_calls(), submits);

    ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
    EXPECT_EQ(adapter->close_steps(), kSkippedClose);
    EXPECT_TRUE(adapter->last_close_panel_closed_in_ar());
  }
}

TEST(QtPanelVisibility, TombstonedPanelIsRefused)
{
  Harness h(PanelCloseMode::Notice);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_TRUE(h.manager->close_panel("p", "the window was closed").ok());
  ASSERT_TRUE(h.manager->is_tombstoned("p"));

  // A notice has no window to hide or show, and nothing is asked of QarOS.
  const qar_ar_core::Result hide = h.manager->set_panel_ar_visible("p", false);
  EXPECT_EQ(hide.status, Status::InvalidState);
  EXPECT_FALSE(hide.message.empty());
  EXPECT_EQ(h.manager->set_panel_ar_visible("p", true).status, Status::InvalidState);
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));

  // A panel hidden before its window closed stays hidden as a notice, and shows its next
  // window when the slot is revived.
  Harness other(PanelCloseMode::Notice);
  SourceWidget first;
  ASSERT_TRUE(other.manager->add_panel("q", first.get(), options_no_pacing()).ok());
  ASSERT_EQ(other.tick(), 1u);
  ASSERT_TRUE(other.manager->set_panel_ar_visible("q", false).ok());
  other.tick();
  ASSERT_TRUE(other.manager->close_panel("q", "the window was closed").ok());
  ASSERT_TRUE(other.manager->is_tombstoned("q"));
  EXPECT_TRUE(other.manager->is_ar_hidden("q"));
  EXPECT_EQ(other.tick(), 0u) << "a hidden notice sends nothing";

  SourceWidget second;
  ASSERT_TRUE(other.manager->add_panel("q", second.get(), options_no_pacing()).ok());
  const auto revived = other.factory->latest();
  EXPECT_EQ(other.tick(), 1u);
  EXPECT_FALSE(other.manager->is_ar_hidden("q"));
  EXPECT_EQ(revived->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_EQ(other.factory->create_calls(), 1u) << "the retired slot was re-used";
}

TEST(QtPanelVisibility, UnknownKeyIsNotFound)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);

  const qar_ar_core::Result hide = h.manager->set_panel_ar_visible("nothing-here", false);
  EXPECT_EQ(hide.status, Status::NotFound);
  EXPECT_NE(hide.message.find("nothing-here"), std::string::npos);
  EXPECT_EQ(h.manager->set_panel_ar_visible("nothing-here", true).status, Status::NotFound);
  EXPECT_FALSE(h.manager->is_ar_hidden("nothing-here"));
  EXPECT_FALSE(h.manager->is_ar_hidden(""));

  // And it touched nothing that exists.
  h.tick();
  EXPECT_EQ(h.factory->latest()->state_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
}

TEST(QtPanelVisibility, DiagnosticsCountHiddenPanels)
{
  Harness h;
  SourceWidget a;
  SourceWidget b;
  ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);
  EXPECT_EQ(h.manager->diagnostics().hidden_panels, 0u);

  ASSERT_TRUE(h.manager->set_panel_ar_visible("a", false).ok());
  h.tick();
  qar_ar_core::PanelDiagnosticsSnapshot snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.hidden_panels, 1u);
  EXPECT_EQ(snapshot.panel_count, 2u);
  EXPECT_EQ(snapshot.live_panels, 2u) << "a hidden panel is still a live window";
  EXPECT_EQ(snapshot.tombstoned_panels, 0u);
  EXPECT_EQ(snapshot.ar_closed_panels, 0u);
  EXPECT_TRUE(h.panel("a").ar_hidden);
  EXPECT_FALSE(h.panel("b").ar_hidden);

  ASSERT_TRUE(h.manager->set_panel_ar_visible("b", false).ok());
  h.tick();
  EXPECT_EQ(h.manager->diagnostics().hidden_panels, 2u);

  ASSERT_TRUE(h.manager->set_panel_ar_visible("a", true).ok());
  h.tick();
  snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.hidden_panels, 1u);
  EXPECT_FALSE(h.panel("a").ar_hidden);
  EXPECT_TRUE(h.panel("b").ar_hidden);
}
