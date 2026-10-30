// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// PanelCloseMode::Park: nothing is closed while the session runs. A Qt-side close parks the
// stream - the panel is hidden in AR through set_state, capture and input stop, and the QarOS
// panel, the video sender and the subscriptions stay - and adding the key again shows the
// same panel again. Also here: a size change in park mode (the QarOS panel it replaces is hidden
// and kept, never closed; the budget of kept panels, and the hide that QarOS refuses being asked
// again), the height factor of dialog panels and the dialog that the cap refuses, the pose hook
// for dialog panels, and the burst and the luminance lines after a long unserved streak.
//
// MockQarPanelSenderAdapter records the state calls and the closes, so what is asserted is
// what the MANAGER owes: it hides instead of closing, falls back to the notice (never to a
// close) when the hide is refused, revives the same sender, and closes everything once at the
// end. What the real qar_gui_panels_set_state does to a panel needs the SDK.

#include <gtest/gtest.h>

#include "qar_qt_streaming/frame_luminance.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QObject>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using qar_ar_core::DiagnosticSeverity;
using qar_ar_core::PanelPointerSample;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_ar_core::QueuedPanelInputSource;
using qar_ar_core::Status;
using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelOverlayAttribution;
using qar_qt_streaming::PanelPointerEvent;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderAdapter;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordingPanelInputSink;

namespace
{

/// A streamable widget: a blue block in a layout, shown under the offscreen platform plugin so
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

/// A 200 x 120 widget of one colour and nothing else, so the luminance of its frame is known.
class SolidWidget
{
public:
  explicit SolidWidget(QColor colour)
  {
    widget_ = new QWidget();
    widget_->setAutoFillBackground(true);
    QPalette palette = widget_->palette();
    palette.setColor(QPalette::Window, colour);
    widget_->setPalette(palette);
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~SolidWidget() { delete widget_; }

  QWidget * get() const { return widget_; }

private:
  QWidget * widget_ = nullptr;
};

/// A 200 x 120 panel whose whole area is one QPushButton, so any point on the panel is a point
/// on the button.
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

/// Pacing off, keepalive on (the shipped default): a panel nobody serves keeps ATTEMPTING a
/// submit on every tick, which is what makes an unserved streak count.
PanelStreamOptions
options_with_keepalive()
{
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  return options;
}

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    DiagnosticSeverity severity;
    std::string category;
    std::string message;
    std::string panel_key;
  };

  void on_message(
    DiagnosticSeverity severity, const std::string & category, const std::string & message,
    const std::string & panel_key) override
  {
    messages.push_back(Entry{severity, category, message, panel_key});
  }

  const Entry * find(const std::string & needle) const
  {
    for (const Entry & entry : messages) {
      if (entry.message.find(needle) != std::string::npos) {
        return &entry;
      }
    }
    return nullptr;
  }

  std::size_t count(const std::string & needle) const
  {
    std::size_t n = 0;
    for (const Entry & entry : messages) {
      if (entry.message.find(needle) != std::string::npos) {
        ++n;
      }
    }
    return n;
  }

  std::vector<Entry> messages;
};

/// A manager wired to the recording factory, a manual clock and a recording sink, in Park mode
/// unless told otherwise, with input pumped by the test (the way the RViz plugin runs it).
struct Harness
{
  explicit Harness(
    PanelCloseMode mode = PanelCloseMode::Park, bool dialogs = false, std::uint32_t cap = 16,
    double dialog_height_factor = 1.0, std::uint32_t replaced_limit = 32)
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    clock = std::make_shared<ManualClock>();
    queue = std::make_shared<QueuedPanelInputSource>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = dialogs;
    config.dialog_height_factor = dialog_height_factor;
    config.diagnostics = &diagnostics;
    config.input_source = queue;
    config.input_sink = &sink;
    config.pump_input_in_tick = false;
    config.close_mode = mode;
    config.max_tombstoned_panels = cap;
    config.max_replaced_panels = replaced_limit;
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  /// One tick, @p dt seconds after the previous one; the number of panels that submitted.
  std::uint32_t tick(double dt = 0.1)
  {
    clock->advance(dt);
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

  /// The adapter minted for @p key, or null.
  std::shared_ptr<MockQarPanelSenderAdapter> adapter(const std::string & key) const
  {
    for (const auto & candidate : factory->created()) {
      if (candidate->desc().key == key) {
        return candidate;
      }
    }
    return nullptr;
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  std::shared_ptr<QueuedPanelInputSource> queue;
  RecordingDiagnostics diagnostics;
  RecordingPanelInputSink sink;
  std::unique_ptr<QtPanelStreamManager> manager;
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

/// A dialog the sweep can find, parented to the OWNER widget so the parent walk alone
/// attributes it. No exec(): setModal-less show() reaches the same state without a nested loop
/// the test runner could not leave.
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

/// Leaves the process-wide attribution state clean around a dialog test.
struct DialogGuard
{
  DialogGuard() { PanelOverlayAttribution::instance().reset(); }
  ~DialogGuard() { PanelOverlayAttribution::instance().reset(); }
};

/// A recording sender that also notes, in a log shared with the test, the order in which QarOS
/// is asked to move a panel ("placement") and to show or hide it ("show", "hide").
class CallOrderAdapter : public MockQarPanelSenderAdapter
{
public:
  CallOrderAdapter(qar_ar_core::PanelDesc desc, std::shared_ptr<std::vector<std::string>> calls)
  : MockQarPanelSenderAdapter(std::move(desc)),
    calls_(std::move(calls))
  {
  }

  bool set_placement(const qar_ar_core::PanelPlacement & placement) override
  {
    calls_->push_back("placement");
    return MockQarPanelSenderAdapter::set_placement(placement);
  }

  bool set_state(QarGuiPanelState state) override
  {
    calls_->push_back(state == QAR_GUI_PANEL_STATE_VISIBLE ? "show" : "hide");
    return MockQarPanelSenderAdapter::set_state(state);
  }

private:
  std::shared_ptr<std::vector<std::string>> calls_;
};

class CallOrderFactory : public qar_ar_core::IQarPanelSenderFactory
{
public:
  std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> create_panel_sender(
    const qar_ar_core::PanelDesc & desc) override
  {
    return std::make_shared<CallOrderAdapter>(desc, calls);
  }

  const std::string & last_error() const override { return error_; }

  std::shared_ptr<std::vector<std::string>> calls = std::make_shared<std::vector<std::string>>();

private:
  std::string error_;
};

const std::vector<QarGuiPanelState> kHiddenOnly = {QAR_GUI_PANEL_STATE_HIDDEN};
const std::vector<QarGuiPanelState> kHiddenThenVisible = {
  QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_VISIBLE};
const std::vector<std::string> kFullClose = {"unsubscribe_input", "close_panel", "destroy_sender"};
const std::vector<std::string> kSkippedClose = {
  "unsubscribe_input", "close_panel_skipped", "destroy_sender"};

/// Gives @p source a new size and ticks (0.2 s apart) until the resize hysteresis has adopted it,
/// which is when the sender is rebuilt for it.
void
resize_and_settle(Harness & h, SourceWidget & source, int width, int height)
{
  source.get()->resize(width, height);
  QCoreApplication::processEvents();
  for (int i = 0; i < 6; ++i) {
    h.tick(0.2);
  }
}

/// How many messages of @p severity the harness's sink has seen.
std::size_t
count_severity(const RecordingDiagnostics & sink, DiagnosticSeverity severity)
{
  std::size_t n = 0;
  for (const auto & entry : sink.messages) {
    if (entry.severity == severity) {
      ++n;
    }
  }
  return n;
}

}  // namespace

// --- the park itself ---------------------------------------------------------

TEST(PanelPark, AQtSideCloseParksTheStreamAndClosesNothing)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("dock");
  ASSERT_NE(adapter, nullptr);
  const std::uint64_t captured = h.panel("dock").frames_captured;
  const std::uint32_t submits = adapter->submit_calls();

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  // Parked from the close on, with the stream, its sender and its QarOS panel still there. No
  // notice, no close; and the hide itself is only a request until the next tick.
  EXPECT_TRUE(h.manager->is_parked("dock"));
  EXPECT_TRUE(h.manager->has_panel("dock"));
  EXPECT_EQ(h.manager->parked_panel_keys(), std::vector<std::string>{"dock"});
  EXPECT_FALSE(h.manager->is_tombstoned("dock"));
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(adapter->state_calls(), 0u);
  EXPECT_FALSE(h.manager->is_ar_hidden("dock"));

  // The next tick carries the hide out, and nothing more is captured or sent.
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(adapter->states(), kHiddenOnly);
  EXPECT_TRUE(h.manager->is_ar_hidden("dock"));
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(h.tick(), 0u);
  }
  EXPECT_EQ(adapter->submit_calls(), submits);
  EXPECT_EQ(h.panel("dock").frames_captured, captured);
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(adapter->state_calls(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u);

  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.panel_count, 1u);
  EXPECT_EQ(snapshot.parked_panels, 1u);
  EXPECT_EQ(snapshot.hidden_panels, 1u);
  EXPECT_EQ(snapshot.live_panels, 1u) << "a parked panel is not a notice";
  EXPECT_EQ(snapshot.tombstoned_panels, 0u);
  EXPECT_EQ(snapshot.parks, 1u);
  EXPECT_EQ(snapshot.park_revivals, 0u);
  EXPECT_EQ(snapshot.panels_closed, 0u);
  EXPECT_EQ(snapshot.tombstones_created, 0u);
  EXPECT_TRUE(h.panel("dock").parked);

  // One line, in the words the log reader greps for.
  const auto * line = h.diagnostics.find("parked 'dock'");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(line->panel_key, "dock");
  EXPECT_EQ(
    line->message,
    "parked 'dock' (reason: closed in RViz): hidden in AR; its sender and QarOS panel are kept "
    "until the session ends");
  EXPECT_EQ(h.diagnostics.count("parked 'dock'"), 1u);
}

TEST(PanelPark, RemovePanelParksToo)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);

  ASSERT_TRUE(h.manager->remove_panel("dock").ok());
  EXPECT_TRUE(h.manager->is_parked("dock"));
  EXPECT_EQ(h.adapter("dock")->close_calls(), 0u);
  EXPECT_NE(h.diagnostics.find("(reason: none given)"), nullptr);
}

TEST(PanelPark, ClosingAParkedKeyAgainChangesNothing)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  ASSERT_TRUE(h.manager->close_panel("dock", "first").ok());
  h.tick();

  EXPECT_TRUE(h.manager->close_panel("dock", "second").ok());
  EXPECT_TRUE(h.manager->remove_panel("dock").ok());
  h.tick();
  EXPECT_EQ(h.diagnostics.count("parked 'dock'"), 1u) << "one line, with the first reason";
  EXPECT_NE(h.diagnostics.find("(reason: first)"), nullptr);
  EXPECT_EQ(h.manager->diagnostics().parks, 1u);
  EXPECT_EQ(h.adapter("dock")->state_calls(), 1u);
  EXPECT_EQ(h.manager->close_panel("nothing-here", "x").status, Status::NotFound);
}

TEST(PanelPark, ASenderlessStreamIsDroppedNotParked)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.factory->create_calls(), 0u) << "no tick yet, so no sender and no QarOS panel";

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_FALSE(h.manager->is_parked("dock"));
  EXPECT_EQ(h.factory->create_calls(), 0u);
  EXPECT_EQ(h.manager->diagnostics().parks, 0u);
  EXPECT_NE(h.diagnostics.find("removed panel stream 'dock'"), nullptr);
  // The key is free: adding it is an ordinary fresh add.
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u);
}

TEST(PanelPark, ParkReleasesAPressedPointerWithoutAClick)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  int clicks = 0;
  QObject::connect(panel.button(), &QPushButton::clicked, [&]() { ++clicks; });

  ASSERT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Started)));
  ASSERT_TRUE(panel.button()->isDown());

  // The pointer is let go of there and then, as a cancel, before the hide is even asked of QarOS.
  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  EXPECT_TRUE(h.manager->is_parked("p"));
  EXPECT_FALSE(panel.button()->isDown());
  EXPECT_EQ(clicks, 0);
  EXPECT_EQ(h.adapter("p")->state_calls(), 0u);

  // And the release that follows reaches nothing.
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("p", PointerPhase::Ended)));
  EXPECT_EQ(clicks, 0);
}

TEST(PanelPark, AParkedStreamIsNotPointedAtEvenBeforeTheHideIsCarriedOut)
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
  // the panel under the title bar.
  const float panel_height_m = h.panel("front").height_meters;
  ASSERT_GT(panel_height_m, static_cast<float>(qar_qt_streaming::kDefaultArFrameTopM));
  const QarVector3 origin{0.0f, -0.5f * panel_height_m, 1.0f};
  const QarVector3 direction{0.0f, 0.0f, -1.0f};

  ASSERT_TRUE(h.manager->route_room_ray(origin, direction, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(h.sink.events().size(), 1u);
  EXPECT_EQ(h.sink.events().back().panel_key, "front");

  // Parked, and no tick yet: the hide has not reached QarOS, but the stream is out of use, so
  // the same ray goes through to the panel behind it and a sample addressed to it lands nowhere.
  ASSERT_TRUE(h.manager->close_panel("front", "closed in RViz").ok());
  ASSERT_TRUE(h.manager->is_parked("front"));
  ASSERT_FALSE(h.manager->is_ar_hidden("front"));
  ASSERT_TRUE(h.manager->route_room_ray(origin, direction, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(h.sink.events().size(), 2u);
  EXPECT_EQ(h.sink.events().back().panel_key, "behind");
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("front", PointerPhase::Instant)));
  EXPECT_EQ(h.sink.events().size(), 2u);
  h.tick();
  EXPECT_FALSE(h.manager->handle_sample(centre_sample("front", PointerPhase::Instant)));
  EXPECT_EQ(h.sink.events().size(), 2u);
}

// --- reviving ----------------------------------------------------------------

TEST(PanelPark, ShowingAParkedPanelWhoseWindowStillExistsRevivesIt)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");
  int clicks = 0;
  QObject::connect(panel.button(), &QPushButton::clicked, [&]() { ++clicks; });
  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));
  const std::uint32_t submits = adapter->submit_calls();

  // Hiding what is hidden asks for nothing.
  EXPECT_TRUE(h.manager->set_panel_ar_visible("p", false).ok());
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 1u);
  EXPECT_TRUE(h.manager->is_parked("p"));

  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  EXPECT_FALSE(h.manager->is_parked("p"));
  EXPECT_EQ(adapter->state_calls(), 1u) << "still only a request";
  // The next tick shows the panel and sends a fresh frame although the widget did not change.
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(adapter->states(), kHiddenThenVisible);
  EXPECT_EQ(adapter->submit_calls(), submits + 1u);
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(h.factory->create_calls(), 1u) << "the QarOS panel and the sender are the old ones";
  EXPECT_EQ(adapter->close_calls(), 0u);

  const auto * line = h.diagnostics.find("revived parked 'p'");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(
    line->message,
    "revived parked 'p': QarOS is asked to show it again (no new QarOS panel)");
  EXPECT_EQ(h.manager->diagnostics().park_revivals, 1u);
  EXPECT_EQ(h.manager->diagnostics().parked_panels, 0u);
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel"), 0u)
    << "a show QarOS accepted has no Warning";

  // It takes input again, on the same window.
  EXPECT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Started)));
  EXPECT_TRUE(panel.button()->isDown());
  EXPECT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Ended)));
  EXPECT_EQ(clicks, 1);
}

TEST(PanelPark, AddingTheKeyAgainRevivesTheStreamOntoAnotherWindowAndPose)
{
  Harness h;
  auto first = std::make_unique<SourceWidget>();
  ASSERT_TRUE(h.manager->add_panel("dock", first->get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("dock");
  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("dock"));

  // The window is destroyed while the stream is parked. Nothing touches it any more.
  first.reset();
  QCoreApplication::processEvents();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(h.tick(), 0u);
  }
  EXPECT_TRUE(h.manager->is_parked("dock"));
  EXPECT_EQ(adapter->close_calls(), 0u);

  // It cannot be shown without a window; it stays parked and nothing is asked of QarOS.
  const qar_ar_core::Result refused = h.manager->set_panel_ar_visible("dock", true);
  EXPECT_EQ(refused.status, Status::InvalidState);
  EXPECT_FALSE(refused.message.empty());
  EXPECT_TRUE(h.manager->is_parked("dock"));
  h.tick();
  EXPECT_EQ(adapter->state_calls(), 1u);

  // Adding the key with another window revives the same panel at the pose given. The add is
  // a desktop click: it makes no QarOS call, the next tick applies the pose and the show.
  SourceWidget second;
  PanelStreamOptions moved = options_no_pacing();
  moved.placement.pose.position = QarVector3{1.5f, 2.0f, -1.0f};
  const std::uint32_t placements = adapter->placement_calls();
  const std::uint32_t state_calls = adapter->state_calls();
  ASSERT_TRUE(h.manager->add_panel("dock", second.get(), moved).ok());
  EXPECT_FALSE(h.manager->is_parked("dock"));
  EXPECT_EQ(adapter->placement_calls(), placements) << "no QarOS call from the click";
  EXPECT_EQ(adapter->state_calls(), state_calls) << "no QarOS call from the click";
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(adapter->placement_calls(), placements + 1u);
  EXPECT_FLOAT_EQ(adapter->placement().pose.position.x, 1.5f);
  EXPECT_FLOAT_EQ(adapter->placement().pose.position.y, 2.0f);
  EXPECT_FLOAT_EQ(adapter->placement().pose.position.z, -1.0f);
  h.tick();
  EXPECT_EQ(adapter->placement_calls(), placements + 1u) << "the pose is carried out once";
  EXPECT_EQ(adapter->states(), kHiddenThenVisible);
  EXPECT_EQ(h.factory->create_calls(), 1u);
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(h.manager->diagnostics().park_revivals, 1u);
  EXPECT_NE(h.diagnostics.find("revived parked 'dock'"), nullptr);

  // It is the live window of the key again: a close parks it once more, and again nothing closes.
  ASSERT_TRUE(h.manager->close_panel("dock", "closed again").ok());
  h.tick();
  EXPECT_TRUE(h.manager->is_parked("dock"));
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(h.manager->diagnostics().parks, 2u);
}

TEST(PanelPark, TheRevivalPoseIsAppliedByTheTickBeforeTheShow)
{
  auto factory = std::make_shared<CallOrderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  config.dialog_panels = false;
  config.close_mode = PanelCloseMode::Park;
  QtPanelStreamManager manager(config);

  SourceWidget source;
  ASSERT_TRUE(manager.add_panel("p", source.get(), options_no_pacing()).ok());
  clock->advance(0.1);
  ASSERT_EQ(manager.tick(), 1u);
  ASSERT_TRUE(manager.close_panel("p", "closed").ok());
  clock->advance(0.1);
  manager.tick();
  ASSERT_EQ(*factory->calls, std::vector<std::string>{"hide"});

  // The revival comes from a desktop click: QarOS is not called from it.
  SourceWidget again;
  PanelStreamOptions moved = options_no_pacing();
  moved.placement.pose.position = QarVector3{0.5f, 1.5f, -2.0f};
  ASSERT_TRUE(manager.add_panel("p", again.get(), moved).ok());
  EXPECT_EQ(*factory->calls, std::vector<std::string>{"hide"});

  // The tick moves the panel first and shows it second, so it is never shown where it hung when it
  // was parked.
  clock->advance(0.1);
  manager.tick();
  EXPECT_EQ(*factory->calls, (std::vector<std::string>{"hide", "placement", "show"}));
  clock->advance(0.1);
  manager.tick();
  EXPECT_EQ(factory->calls->size(), 3u) << "once";
}

TEST(PanelPark, ARevivalBeforeTheHideWasCarriedOutAsksQarOSForNothing)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");

  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  h.tick();

  EXPECT_FALSE(h.manager->is_parked("p"));
  EXPECT_EQ(adapter->state_calls(), 0u) << "never hidden, so nothing to show";
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel"), 0u);
}

TEST(PanelPark, ARefusedShowAfterARevivalIsOneWarningNamingTheReason)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");
  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));

  // Shown again from the host's page: QarOS refuses.
  adapter->fail_next_set_state();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel"), 0u)
    << "nothing is known until the tick has asked QarOS";
  h.tick();
  const auto * warning = h.diagnostics.find("could not show the revived panel");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  EXPECT_EQ(
    warning->message,
    "could not show the revived panel 'p' in AR (injected set_state failure)");
  EXPECT_TRUE(h.manager->is_ar_hidden("p")) << "the panel is still down";
  EXPECT_EQ(h.panel("p").visibility_failures, 1u);
  for (int i = 0; i < 4; ++i) {
    h.tick();
  }
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel"), 1u) << "once per revival";

  // The same through the key's window being added again, with the refusal on the show.
  ASSERT_TRUE(h.manager->close_panel("p", "stopped again").ok());
  h.tick();
  adapter->fail_next_set_state();
  SourceWidget again;
  ASSERT_TRUE(h.manager->add_panel("p", again.get(), options_no_pacing()).ok());
  h.tick();
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel 'p' in AR"), 2u);
  EXPECT_EQ(h.panel("p").visibility_failures, 2u);

  // And a show that works says nothing more.
  ASSERT_TRUE(h.manager->close_panel("p", "stopped a third time").ok());
  h.tick();
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  EXPECT_FALSE(h.manager->is_ar_hidden("p"));
  EXPECT_EQ(h.diagnostics.count("could not show the revived panel"), 2u);
}

TEST(PanelPark, ASenderThatCannotShowAPanelAgainGivesItsReasonInTheWarning)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");
  ASSERT_TRUE(h.manager->close_panel("p", "stopped").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_ar_hidden("p"));

  adapter->set_supports_state(false);
  ASSERT_TRUE(h.manager->set_panel_ar_visible("p", true).ok());
  h.tick();
  const auto * warning = h.diagnostics.find("could not show the revived panel");
  ASSERT_NE(warning, nullptr);
  EXPECT_NE(warning->message.find("cannot change the state"), std::string::npos)
    << warning->message;
  EXPECT_EQ(adapter->state_calls(), 1u) << "a sender that says it cannot is not asked";
}

TEST(PanelPark, ReviveIsAllowedWhateverTheCapAndFreesItsSlot)
{
  Harness h(PanelCloseMode::Park, false, /*cap=*/2);
  SourceWidget a;
  SourceWidget b;
  SourceWidget c;
  ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);
  ASSERT_TRUE(h.manager->close_panel("a", "closed").ok());
  ASSERT_TRUE(h.manager->close_panel("b", "closed").ok());
  h.tick();

  // Two parked panels fill the cap: no further stream is created, and a Warning says so.
  const qar_ar_core::Result refused = h.manager->add_panel("c", c.get(), options_no_pacing());
  EXPECT_EQ(refused.status, Status::InvalidState);
  EXPECT_FALSE(h.manager->has_panel("c"));
  EXPECT_GE(h.manager->diagnostics().tombstone_cap_refusals, 1u);
  const auto * warning = h.diagnostics.find("this session is holding 2 panels");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(h.factory->create_calls(), 2u);

  // Reviving a parked key creates nothing, so the full cap does not stop it...
  SourceWidget a_again;
  ASSERT_TRUE(h.manager->add_panel("a", a_again.get(), options_no_pacing()).ok());
  EXPECT_FALSE(h.manager->is_parked("a"));
  // ...and it frees a slot, so the stream that was refused now fits.
  ASSERT_TRUE(h.manager->add_panel("c", c.get(), options_no_pacing()).ok());
  h.tick();
  EXPECT_EQ(h.factory->create_calls(), 3u);
  EXPECT_EQ(h.manager->parked_panel_keys(), std::vector<std::string>{"b"});
}

TEST(PanelPark, ParkedAndNoticePanelsCountTogetherAgainstTheCap)
{
  Harness h(PanelCloseMode::Park, false, /*cap=*/2);
  SourceWidget a;
  SourceWidget b;
  SourceWidget c;
  ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);

  ASSERT_TRUE(h.manager->close_panel("a", "closed").ok());   // parked
  h.manager->set_close_mode(PanelCloseMode::Notice);
  ASSERT_TRUE(h.manager->close_panel("b", "closed").ok());   // a notice
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("a"));
  ASSERT_TRUE(h.manager->is_tombstoned("b"));

  EXPECT_EQ(
    h.manager->add_panel("c", c.get(), options_no_pacing()).status, Status::InvalidState);
  EXPECT_EQ(h.factory->create_calls(), 2u);
}

// --- the hide is refused -----------------------------------------------------

TEST(PanelPark, ARefusedHideFallsBackToTheNoticeNeverToAClose)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");
  adapter->fail_next_set_state();

  ASSERT_TRUE(
    h.manager->close_panel("p", "the window was closed", "This window has been closed.").ok());
  EXPECT_TRUE(h.manager->is_parked("p")) << "until the tick that asks QarOS to hide it";
  h.tick();

  EXPECT_FALSE(h.manager->is_parked("p"));
  EXPECT_TRUE(h.manager->is_tombstoned("p"));
  EXPECT_TRUE(h.manager->has_panel("p"));
  EXPECT_EQ(adapter->close_calls(), 0u) << "a refused hide never becomes a close";
  EXPECT_EQ(adapter->state_calls(), 1u);
  const auto * warning = h.diagnostics.find("could not hide 'p' in AR");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  EXPECT_NE(warning->message.find("injected set_state failure"), std::string::npos)
    << warning->message;
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.parked_panels, 0u);
  EXPECT_EQ(snapshot.tombstoned_panels, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 1u);
  EXPECT_EQ(snapshot.panels_closed, 0u);

  // The notice is what the panel streams from here on, and it is reused like any retired slot.
  EXPECT_EQ(h.tick(), 1u);
  SourceWidget again;
  ASSERT_TRUE(h.manager->add_panel("p", again.get(), options_no_pacing()).ok());
  EXPECT_FALSE(h.manager->is_tombstoned("p"));
  EXPECT_EQ(h.factory->create_calls(), 1u);
}

TEST(PanelPark, ASenderThatCannotHideAPanelFallsBackToTheNoticeToo)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");
  adapter->set_supports_state(false);

  ASSERT_TRUE(h.manager->close_panel("p", "the window was closed").ok());
  h.tick();

  EXPECT_TRUE(h.manager->is_tombstoned("p"));
  EXPECT_FALSE(h.manager->is_parked("p"));
  EXPECT_EQ(adapter->close_calls(), 0u);
  EXPECT_EQ(adapter->state_calls(), 0u) << "a sender that says it cannot is not asked";
  const auto * warning = h.diagnostics.find("could not hide 'p' in AR");
  ASSERT_NE(warning, nullptr);
  EXPECT_NE(warning->message.find("cannot change the state"), std::string::npos)
    << warning->message;
}

// --- a panel the operator closed in AR ----------------------------------------

TEST(PanelPark, APanelClosedInArIsParkedWithoutAHideCallAndShowIsTriedOnRevival)
{
  {
    Harness h;
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    const auto adapter = h.adapter("p");
    ASSERT_TRUE(h.manager->note_ar_panel_closed("p"));

    ASSERT_TRUE(h.manager->close_panel("p", "closed in RViz").ok());
    h.tick();
    EXPECT_TRUE(h.manager->is_parked("p"));
    EXPECT_TRUE(h.manager->is_ar_hidden("p"));
    EXPECT_EQ(adapter->state_calls(), 0u) << "the operator took the panel down already";
    EXPECT_EQ(adapter->close_calls(), 0u);

    // A revival tries to show it, as showing a panel closed in AR does; accepted, it is live.
    SourceWidget again;
    ASSERT_TRUE(h.manager->add_panel("p", again.get(), options_no_pacing()).ok());
    EXPECT_EQ(h.tick(), 1u);
    EXPECT_EQ(adapter->states(), (std::vector<QarGuiPanelState>{QAR_GUI_PANEL_STATE_VISIBLE}));
    const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
    EXPECT_FALSE(d.ar_panel_closed);
    EXPECT_FALSE(d.ar_hidden);
    EXPECT_FALSE(d.parked);
    EXPECT_EQ(adapter->close_calls(), 0u);
  }

  // Refused, the panel stays down: closed in AR and hidden, with the refusal counted.
  {
    Harness h;
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    const auto adapter = h.adapter("p");
    ASSERT_TRUE(h.manager->note_ar_panel_closed("p"));
    ASSERT_TRUE(h.manager->close_panel("p", "closed in RViz").ok());
    h.tick();

    adapter->fail_next_set_state();
    SourceWidget again;
    ASSERT_TRUE(h.manager->add_panel("p", again.get(), options_no_pacing()).ok());
    EXPECT_EQ(h.tick(), 0u);
    const qar_ar_core::PanelStreamDiagnostics d = h.panel("p");
    EXPECT_TRUE(d.ar_panel_closed);
    EXPECT_TRUE(d.ar_hidden);
    EXPECT_EQ(d.visibility_failures, 1u);

    // With a sender that cannot detach (the test sender's default), the session end closes
    // it without a second close_panel.
    h.manager->clear();
    EXPECT_EQ(adapter->close_steps(), kSkippedClose);
  }
}

TEST(PanelPark, AnArCloseStillInTheQueueIsLearntBeforeThePark)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");

  // The operator's close has been pushed by the runtime and no tick has drained it yet.
  h.queue->push_closed_panel("p");
  ASSERT_TRUE(h.manager->close_panel("p", "closed in RViz").ok());
  h.tick();

  EXPECT_EQ(adapter->state_calls(), 0u);
  EXPECT_TRUE(h.manager->is_parked("p"));
  EXPECT_TRUE(h.panel("p").ar_panel_closed);
}

// --- dialogs -----------------------------------------------------------------

TEST(PanelPark, ADismissedDialogParksAndOpeningItAgainRevivesTheSameSender)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.tick();
  const std::uint32_t after_owner = h.factory->create_calls();

  for (int round = 0; round < 4; ++round) {
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    h.tick();
    ASSERT_EQ(h.manager->dialog_panel_keys(), std::vector<std::string>{"dialog:Add"})
      << "round " << round << ": the key is reused, so the name never grows a suffix";
    EXPECT_FALSE(h.manager->is_parked("dialog:Add")) << "round " << round;
    // Idle ticks while it is open do not mint a second panel for it.
    h.tick();
    h.tick();
    EXPECT_EQ(h.manager->dialog_panel_keys().size(), 1u) << "round " << round;

    dialog->hide();
    QCoreApplication::processEvents();
    h.tick();
    EXPECT_TRUE(h.manager->dialog_panel_keys().empty()) << "round " << round;
    EXPECT_TRUE(h.manager->is_parked("dialog:Add")) << "round " << round;
    EXPECT_TRUE(h.manager->is_ar_hidden("dialog:Add")) << "round " << round;
    delete dialog;
    QCoreApplication::processEvents();
  }

  // Four showings, one sender and one QarOS panel, none of them ever closed.
  EXPECT_EQ(h.factory->create_calls(), after_owner + 1u);
  const auto dialog_adapter = h.adapter("dialog:Add");
  ASSERT_NE(dialog_adapter, nullptr);
  EXPECT_EQ(dialog_adapter->close_calls(), 0u);
  EXPECT_EQ(h.adapter("p")->close_calls(), 0u);
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.parks, 4u);
  EXPECT_EQ(snapshot.park_revivals, 3u);
  EXPECT_EQ(snapshot.parked_panels, 1u);
  EXPECT_EQ(snapshot.panels_closed, 0u);
  EXPECT_EQ(snapshot.tombstones_created, 0u);
  EXPECT_EQ(h.manager->panel_keys().size(), 2u);
  // Hidden after each dismissal and shown after each reopening.
  EXPECT_EQ(
    dialog_adapter->states(),
    (std::vector<QarGuiPanelState>{
      QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_VISIBLE, QAR_GUI_PANEL_STATE_HIDDEN,
      QAR_GUI_PANEL_STATE_VISIBLE, QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_VISIBLE,
      QAR_GUI_PANEL_STATE_HIDDEN}));
}

TEST(PanelPark, ADestroyedDialogParksBeforeAnythingPaintsThroughIt)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  h.tick();
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));

  // Deleted outright, the way RViz disposes of a one-shot dialog.
  delete dialog;
  QCoreApplication::processEvents();
  h.tick();
  EXPECT_TRUE(h.manager->is_parked("dialog:Add"));
  EXPECT_TRUE(h.manager->dialog_panel_keys().empty());
  EXPECT_EQ(h.adapter("dialog:Add")->close_calls(), 0u);

  // Ticking on does not fault, and a dialog of the same name revives the stream.
  h.tick();
  h.tick();
  QDialog * again = show_dialog(panel.widget(), QSize(240, 100), "Add");
  h.tick();
  EXPECT_FALSE(h.manager->is_parked("dialog:Add"));
  EXPECT_EQ(h.factory->create_calls(), 2u);
  again->hide();
  QCoreApplication::processEvents();
  delete again;
  QCoreApplication::processEvents();
}

TEST(PanelPark, ParkingTheOwnerParksItsDialogsToo)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.tick();
  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  h.tick();
  ASSERT_TRUE(h.manager->has_panel("dialog:Add"));

  ASSERT_TRUE(h.manager->remove_panel("p").ok());
  h.tick();

  // The dialog was only ever an extension of the stopped panel: parked with it, and the sweep
  // does not take the dialog, which is still on screen, for a new one.
  EXPECT_TRUE(h.manager->is_parked("p"));
  EXPECT_TRUE(h.manager->is_parked("dialog:Add"));
  EXPECT_TRUE(h.manager->dialog_panel_keys().empty());
  h.tick();
  h.tick();
  EXPECT_EQ(h.factory->create_calls(), 2u);
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u) << adapter->desc().key;
  }

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

// --- inside the panel's own handler -------------------------------------------

TEST(PanelPark, AParkAskedForFromThePanelsOwnClickRunsOnceTheClickHasReturned)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto adapter = h.adapter("p");

  // The Stop button, pressed in AR on the very panel it stops: the handler runs INSIDE the
  // stream's own deliver_input, with the injector on the stack beneath it.
  qar_ar_core::Result asked;
  bool parked_inside_handler = true;
  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      asked = h.manager->close_panel("p", "stopped in RViz");
      parked_inside_handler = h.manager->is_parked("p");
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_sample("p", PointerPhase::Instant)));

  EXPECT_TRUE(asked.ok()) << "from the caller's side the close succeeded";
  EXPECT_FALSE(parked_inside_handler) << "the stream is not changed under its own handler";
  EXPECT_TRUE(h.manager->is_parked("p")) << "parked on the way out of the outermost frame";
  EXPECT_EQ(h.manager->diagnostics().closes_deferred, 1u);
  EXPECT_NE(h.diagnostics.find("was asked for from inside a pointer action"), nullptr);
  EXPECT_EQ(adapter->close_calls(), 0u);
  h.tick();
  EXPECT_EQ(adapter->states(), kHiddenOnly);
}

// --- the mode switch ----------------------------------------------------------

TEST(PanelPark, SetCloseModeAppliesToTheClosesThatFollow)
{
  Harness h(PanelCloseMode::Destroy);
  SourceWidget a;
  SourceWidget b;
  ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);

  ASSERT_TRUE(h.manager->close_panel("a", "closed").ok());
  EXPECT_FALSE(h.manager->has_panel("a")) << "destroy mode closes";
  EXPECT_EQ(h.adapter("a")->close_calls(), 1u);

  h.manager->set_close_mode(PanelCloseMode::Park);
  EXPECT_EQ(h.manager->close_mode(), PanelCloseMode::Park);
  ASSERT_TRUE(h.manager->close_panel("b", "closed").ok());
  EXPECT_TRUE(h.manager->is_parked("b"));
  EXPECT_EQ(h.adapter("b")->close_calls(), 0u);

  // Back to destroy: what is parked stays parked, and closing it then really closes it.
  h.manager->set_close_mode(PanelCloseMode::Destroy);
  h.tick();
  EXPECT_TRUE(h.manager->is_parked("b"));
  ASSERT_TRUE(h.manager->close_panel("b", "closed for good").ok());
  EXPECT_FALSE(h.manager->has_panel("b"));
  EXPECT_EQ(h.adapter("b")->close_steps(), kFullClose);
  EXPECT_EQ(h.adapter("b")->close_calls(), 1u);
}

TEST(PanelPark, SessionEndClosesEveryParkedPanelExactlyOnce)
{
  std::shared_ptr<MockQarPanelSenderFactory> factory;
  {
    Harness h;
    SourceWidget a;
    SourceWidget b;
    SourceWidget live;
    ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
    ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
    ASSERT_TRUE(h.manager->add_panel("live", live.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 3u);
    ASSERT_TRUE(h.manager->close_panel("a", "closed").ok());
    ASSERT_TRUE(h.manager->note_ar_panel_closed("b"));
    ASSERT_TRUE(h.manager->close_panel("b", "closed").ok());
    h.tick();
    ASSERT_EQ(h.adapter("a")->close_calls(), 0u);
    ASSERT_EQ(h.adapter("b")->close_calls(), 0u);

    h.manager->clear();
    EXPECT_EQ(h.adapter("a")->close_steps(), kFullClose);
    EXPECT_EQ(h.adapter("b")->close_steps(), kSkippedClose) << "the operator closed it already";
    EXPECT_EQ(h.adapter("live")->close_steps(), kFullClose);
    EXPECT_TRUE(h.manager->panel_keys().empty());
    EXPECT_TRUE(h.manager->parked_panel_keys().empty());

    // Nothing is closed twice, by a second clear() or by the destructor.
    h.manager->clear();
    for (const auto & adapter : h.factory->created()) {
      EXPECT_EQ(adapter->close_calls(), 1u) << adapter->desc().key;
      EXPECT_EQ(adapter->sender_destroy_calls(), 1u) << adapter->desc().key;
    }
    factory = h.factory;
  }
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u) << adapter->desc().key;
  }
}

// --- a size change in park mode: the old QarOS panel is hidden and kept ---------

TEST(PanelParkResize, TheOldPanelIsHiddenAndKeptAndANewOneIsMadeWithoutAClose)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 0u) << "the first sender is no rebuild";
  EXPECT_EQ(h.manager->replaced_panel_count(), 0u);

  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->create_calls(), 2u);
  const auto old_panel = h.factory->created()[0];
  const auto new_panel = h.factory->created()[1];

  // The old panel: hidden once, never closed. The new one: its own, at the new size, shown.
  EXPECT_EQ(old_panel->states(), kHiddenOnly);
  EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(old_panel->close_calls(), 0u);
  EXPECT_EQ(old_panel->sender_destroy_calls(), 0u);
  EXPECT_EQ(new_panel->desc().buffer_width, 400u);
  EXPECT_EQ(new_panel->state_calls(), 0u);
  EXPECT_EQ(new_panel->close_calls(), 0u);
  EXPECT_GT(new_panel->submit_calls(), 0u) << "the stream goes on, on the new panel";
  EXPECT_EQ(h.factory->events(), (std::vector<std::string>{"create p", "create p"}))
    << "no close between the two creates";
  EXPECT_FALSE(h.manager->is_ar_hidden("p")) << "the stream shows its new panel";
  EXPECT_EQ(h.panel("p").adapter_rebuilds, 2u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(h.manager->diagnostics().panels_closed, 0u);

  // One Info line, in the words the log reader greps for, and nothing louder.
  const auto * line = h.diagnostics.find("changed size");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(line->panel_key, "p");
  EXPECT_EQ(
    line->message,
    "'p' changed size: its old QarOS panel is hidden and kept until the session ends, a new one "
    "is made (no close)");
  EXPECT_EQ(h.diagnostics.count("changed size"), 1u);
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 0u);

  // A second size change keeps a second panel; still nothing is closed. The first kept panel was
  // hidden, so it is not asked again however long the session goes on.
  resize_and_settle(h, source, 300, 200);
  ASSERT_EQ(h.factory->create_calls(), 3u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 2u);
  EXPECT_EQ(h.factory->created()[1]->states(), kHiddenOnly);
  for (int i = 0; i < 50; ++i) {
    h.tick(0.1);
  }
  EXPECT_EQ(old_panel->states(), kHiddenOnly) << "a panel that was hidden is not asked again";
  EXPECT_EQ(h.factory->created()[1]->states(), kHiddenOnly);
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u);
  }
  EXPECT_EQ(h.diagnostics.count("changed size"), 2u);
  EXPECT_EQ(h.diagnostics.count("is hidden now"), 0u);

  // With a sender that cannot detach (the test sender's default), the session end closes
  // every panel, the kept ones too, each exactly once.
  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
    EXPECT_EQ(adapter->close_steps(), kFullClose);
    EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  }
  const auto * ended = h.diagnostics.find("closed the 2 earlier QarOS panel(s) of 'p'");
  ASSERT_NE(ended, nullptr);
  EXPECT_EQ(ended->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(
    ended->message,
    "closed the 2 earlier QarOS panel(s) of 'p' that size changes had replaced and kept");
  EXPECT_EQ(h.manager->replaced_panel_count(), 0u);
  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u) << "a second clear closes nothing again";
  }
}

TEST(PanelParkResize, TheKeptPanelsAreClosedOnceEvenWhenTheManagerIsDestroyed)
{
  std::shared_ptr<MockQarPanelSenderFactory> factory;
  {
    Harness h;
    SourceWidget source(200, 120);
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    resize_and_settle(h, source, 400, 240);
    ASSERT_EQ(h.factory->create_calls(), 2u);
    factory = h.factory;
  }
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u) << adapter->desc().buffer_width;
    EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  }
}

TEST(PanelParkResize, ADestroyModeCloseOfTheStreamClosesTheKeptPanelsToo)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.manager->replaced_panel_count(), 1u);

  // The mode is switched, and the stream is closed for good: nothing is left behind.
  h.manager->set_close_mode(PanelCloseMode::Destroy);
  ASSERT_TRUE(h.manager->close_panel("p", "closed for good").ok());
  EXPECT_FALSE(h.manager->has_panel("p"));
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
    EXPECT_EQ(adapter->close_steps(), kFullClose);
  }
  EXPECT_NE(
    h.diagnostics.find("closed the 1 earlier QarOS panel(s) of 'p' that size changes had replaced"),
    nullptr);
  EXPECT_EQ(h.manager->replaced_panel_count(), 0u);
}

TEST(PanelParkResize, TheOtherModesStillCloseTheOldPanelAndSayNothing)
{
  for (const PanelCloseMode mode : {PanelCloseMode::Destroy, PanelCloseMode::Notice}) {
    Harness h(mode);
    SourceWidget source(200, 120);
    ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    EXPECT_EQ(h.diagnostics.count("changed size"), 0u) << "the first sender is no rebuild";

    resize_and_settle(h, source, 400, 240);
    ASSERT_EQ(h.factory->create_calls(), 2u) << to_string(mode);
    const auto old_panel = h.factory->created()[0];
    EXPECT_EQ(old_panel->close_calls(), 1u) << to_string(mode);
    EXPECT_EQ(old_panel->close_steps(), kFullClose);
    EXPECT_EQ(old_panel->state_calls(), 0u) << "no hide: the panel is closed";
    EXPECT_EQ(
      h.factory->events(), (std::vector<std::string>{"create p", "close p", "create p"}));
    EXPECT_EQ(h.manager->replaced_panel_count(), 0u);
    EXPECT_EQ(h.diagnostics.count("changed size"), 0u) << to_string(mode);
  }
}

TEST(PanelParkResize, AHideQarOSRefusesLeavesTheOldPanelShownAndKeptNeverClosed)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  h.factory->created()[0]->fail_next_set_state();

  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->create_calls(), 2u);
  const auto old_panel = h.factory->created()[0];
  EXPECT_EQ(old_panel->state_calls(), 1u) << "the hide was asked for, once";
  EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_VISIBLE) << "and refused";
  EXPECT_EQ(old_panel->close_calls(), 0u) << "a refused hide never turns into a close";
  EXPECT_EQ(h.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(h.factory->events(), (std::vector<std::string>{"create p", "create p"}));

  const auto * warning = h.diagnostics.find("changed size");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  EXPECT_EQ(
    warning->message,
    "'p' changed size: its old QarOS panel could not be hidden (injected set_state failure), so "
    "it stays visible and is kept until the session ends; a new one is made (no close)");
  EXPECT_EQ(h.diagnostics.count("changed size"), 1u);

  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
  }
}

TEST(PanelParkResize, ASenderThatCannotHideAPanelIsNotAskedAndItsOldPanelIsKept)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  h.factory->created()[0]->set_supports_state(false);

  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(h.factory->created()[0]->state_calls(), 0u);
  EXPECT_EQ(h.factory->created()[0]->close_calls(), 0u);
  const auto * warning = h.diagnostics.find("changed size");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_NE(
    warning->message.find("could not be hidden (the panel sender cannot change the state of a "
    "panel)"),
    std::string::npos) << warning->message;

  // A sender that cannot hide a panel at all has nothing to be asked again.
  for (int i = 0; i < 60; ++i) {
    h.tick(0.1);
  }
  EXPECT_EQ(h.factory->created()[0]->state_calls(), 0u);
  EXPECT_EQ(h.diagnostics.count("still visible after"), 0u);
}

TEST(PanelParkResize, WhenTheLimitOfKeptPanelsIsReachedTheRebuildClosesAsBeforeAndTheWarningSaysSo)
{
  // Two kept panels at most; the cap of parked panels is out of the picture.
  Harness h(PanelCloseMode::Park, false, /*cap=*/16, /*dialog_height_factor=*/1.0, /*limit=*/2);
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);

  // The first size change fits under the limit and keeps its old panel, in an Info line.
  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(h.factory->created()[0]->close_calls(), 0u);
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 0u);

  // The second takes the last slot: the old panel is kept all the same, and the line is a
  // Warning that says the next ones close.
  resize_and_settle(h, source, 300, 200);
  ASSERT_EQ(h.factory->create_calls(), 3u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 2u);
  EXPECT_EQ(h.factory->created()[1]->states(), kHiddenOnly);
  EXPECT_EQ(h.factory->created()[1]->close_calls(), 0u) << "this one is still kept";
  const auto * early = h.diagnostics.find("This session now keeps 2 replaced QarOS panels");
  ASSERT_NE(early, nullptr);
  EXPECT_EQ(early->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(early->panel_key, "p");
  EXPECT_EQ(
    early->message,
    "'p' changed size: its old QarOS panel is hidden and kept until the session ends, a new one "
    "is made (no close). This session now keeps 2 replaced QarOS panels, the limit "
    "(max_replaced_panels): the next size changes close their old QarOS panel, and in park mode "
    "that close may crash the desktop Visualizer.");
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 1u);

  // The third finds the limit reached: the panel it replaces is closed, as in the other modes,
  // and the Warning says why.
  resize_and_settle(h, source, 500, 300);
  ASSERT_EQ(h.factory->create_calls(), 4u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 2u) << "nothing more was kept";
  EXPECT_EQ(h.factory->created()[0]->close_calls(), 0u) << "the kept panels stay";
  EXPECT_EQ(h.factory->created()[1]->close_calls(), 0u);
  EXPECT_EQ(h.factory->created()[2]->close_calls(), 1u) << "the replaced one is closed";
  EXPECT_EQ(h.factory->created()[2]->state_calls(), 0u);
  EXPECT_EQ(
    h.factory->events(),
    (std::vector<std::string>{"create p", "create p", "create p", "close p", "create p"}));
  const auto * warning = h.diagnostics.find("its QarOS panel is rebuilt");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  EXPECT_EQ(
    warning->message,
    "'p' changed size: its QarOS panel is rebuilt (closed and created again); in park mode that "
    "close may crash the desktop Visualizer. The old panel was not kept hidden: this session "
    "already keeps 2 replaced QarOS panels, the limit (max_replaced_panels).");
  EXPECT_EQ(h.diagnostics.count("changed size"), 3u) << "one Info line and two Warnings";
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 2u);

  // With a sender that cannot detach (the test sender's default), every panel is closed.
  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u) << "every panel is closed exactly once in all";
  }
}

TEST(PanelParkResize, ARefusedHideThatTakesTheLastSlotSaysThatTheNextOnesClose)
{
  Harness h(PanelCloseMode::Park, false, /*cap=*/16, /*dialog_height_factor=*/1.0, /*limit=*/1);
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  h.factory->created()[0]->fail_next_set_state();

  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.manager->replaced_panel_count(), 1u);
  const auto * warning = h.diagnostics.find("changed size");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_NE(warning->message.find("could not be hidden (injected set_state failure)"),
    std::string::npos) << warning->message;
  EXPECT_NE(
    warning->message.find("the limit (max_replaced_panels): the next size changes close"),
    std::string::npos) << warning->message;
  EXPECT_EQ(h.diagnostics.count("changed size"), 1u);
}

TEST(PanelParkResize, ALimitOfZeroKeepsEveryOldPanelAndWarnsOfNothing)
{
  Harness h(PanelCloseMode::Park, false, /*cap=*/16, /*dialog_height_factor=*/1.0, /*limit=*/0);
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  for (int i = 0; i < 5; ++i) {
    resize_and_settle(h, source, 300 + 100 * i, 200 + 60 * i);
  }
  EXPECT_EQ(h.factory->create_calls(), 6u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 5u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 5u);
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 0u);
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u);
  }
  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
  }
}

TEST(PanelParkResize, TheKeptPanelsHaveABudgetOfTheirOwnAndNeverBlockANewStream)
{
  // The cap of parked panels is 2; three kept panels are more than that.
  Harness h(PanelCloseMode::Park, false, /*cap=*/2);
  SourceWidget source(200, 120);
  SourceWidget parked_one(200, 120);
  SourceWidget fresh(200, 120);
  SourceWidget refused(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("parked", parked_one.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 2u);
  ASSERT_TRUE(h.manager->close_panel("parked", "closed").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("parked"));

  resize_and_settle(h, source, 400, 240);
  resize_and_settle(h, source, 300, 200);
  resize_and_settle(h, source, 500, 300);
  ASSERT_EQ(h.manager->replaced_panel_count(), 3u);

  // One parked panel of two, whatever the kept panels are: a new stream is made.
  ASSERT_TRUE(h.manager->add_panel("q", fresh.get(), options_no_pacing()).ok());
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 0u);
  EXPECT_EQ(h.diagnostics.count("this session is holding"), 0u);

  // The parked panels alone fill the cap: now a new stream is refused, and the Warning does not
  // count the kept panels among the held ones.
  h.tick();
  ASSERT_NE(h.adapter("q"), nullptr) << "the new stream has its QarOS panel, so it can be parked";
  ASSERT_TRUE(h.manager->close_panel("q", "closed").ok());
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("q"));
  EXPECT_EQ(
    h.manager->add_panel("r", refused.get(), options_no_pacing()).status, Status::InvalidState);
  EXPECT_GE(h.manager->diagnostics().tombstone_cap_refusals, 1u);
  EXPECT_FALSE(h.manager->has_panel("r"));
  const auto * cap = h.diagnostics.find("this session is holding 2 panels");
  ASSERT_NE(cap, nullptr);
  EXPECT_EQ(cap->message.find("size changes replaced"), std::string::npos) << cap->message;

  // Reviving a parked panel frees a slot, and the stream that was refused fits now.
  SourceWidget again(200, 120);
  ASSERT_TRUE(h.manager->add_panel("parked", again.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("r", refused.get(), options_no_pacing()).ok());
}

TEST(PanelParkResize, ARefusedHideOfAKeptPanelIsAskedAgainAndALaterSuccessIsLogged)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto old_panel = h.factory->created()[0];
  old_panel->fail_next_set_state();

  // The size change: the hide is asked for and refused (the one fault above), the Warning says
  // so, and nothing is asked again before a second has passed.
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  double rebuilt_at = -1.0;
  double retried_at = -1.0;
  for (int i = 0; i < 60 && retried_at < 0.0; ++i) {
    h.tick(0.1);
    if (rebuilt_at < 0.0 && h.factory->create_calls() == 2u) {
      rebuilt_at = h.clock->now_seconds();
      EXPECT_EQ(old_panel->state_calls(), 1u) << "asked once, at the size change";
      EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_VISIBLE) << "and refused";
    }
    if (old_panel->state_calls() == 2u) {
      retried_at = h.clock->now_seconds();
    }
  }
  ASSERT_GE(rebuilt_at, 0.0);
  ASSERT_GE(retried_at, 0.0) << "the hide was asked for again";
  EXPECT_GE(retried_at - rebuilt_at, 1.0 - 1e-6) << "a second apart at the least";
  EXPECT_LE(retried_at - rebuilt_at, 1.25);

  // This time QarOS takes the hide: the kept panel is hidden, and the log says so once.
  EXPECT_EQ(old_panel->states(), (std::vector<QarGuiPanelState>{
    QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_HIDDEN}));
  EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(old_panel->close_calls(), 0u) << "a retried hide is never a close";
  const auto * late = h.diagnostics.find("is hidden now (attempt 2 of 4)");
  ASSERT_NE(late, nullptr);
  EXPECT_EQ(late->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(late->panel_key, "p");
  EXPECT_EQ(
    late->message,
    "'p' the old QarOS panel (stream " + old_panel->stream_id_text() +
    ") that a size change replaced is hidden now (attempt 2 of 4); it is kept until the session "
    "ends");
  EXPECT_EQ(h.diagnostics.count("is hidden now"), 1u);

  // A hidden panel is not asked again.
  for (int i = 0; i < 40; ++i) {
    h.tick(0.1);
  }
  EXPECT_EQ(old_panel->state_calls(), 2u);
  EXPECT_EQ(h.diagnostics.count("is hidden now"), 1u);
  EXPECT_EQ(h.diagnostics.count("still visible after"), 0u);

  h.manager->clear();
  EXPECT_EQ(old_panel->close_calls(), 1u);
}

TEST(PanelParkResize, TheRetriesGoOnWhileTheStreamIsParked)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto old_panel = h.factory->created()[0];
  old_panel->fail_next_set_state();
  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->create_calls(), 2u);
  ASSERT_EQ(old_panel->state_calls(), 1u);

  // The window closes and the stream is parked (hidden): the kept panel that stayed visible is
  // asked again all the same, for it belongs to the stream and not to the panel it shows now.
  ASSERT_TRUE(h.manager->close_panel("p", "closed").ok());
  for (int i = 0; i < 40; ++i) {
    h.tick(0.1);
  }
  ASSERT_TRUE(h.manager->is_parked("p"));
  EXPECT_EQ(old_panel->state_calls(), 2u);
  EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(h.diagnostics.count("is hidden now (attempt 2 of 4)"), 1u);
  EXPECT_EQ(old_panel->close_calls(), 0u);
}

TEST(PanelParkResize, ARefusedHideIsAskedForThreeMoreTimesAtMostAndTheEndIsAWarning)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  const auto old_panel = h.factory->created()[0];
  old_panel->fail_next_set_state(100);  // QarOS never takes it

  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  std::vector<double> asked_at;
  std::uint32_t seen = 0;
  for (int i = 0; i < 200; ++i) {
    h.tick(0.1);
    while (seen < old_panel->state_calls()) {
      asked_at.push_back(h.clock->now_seconds());
      ++seen;
    }
  }
  // The attempt of the size change and three more, none closing anything.
  ASSERT_EQ(old_panel->state_calls(), 4u);
  ASSERT_EQ(asked_at.size(), 4u);
  for (std::size_t i = 1; i < asked_at.size(); ++i) {
    EXPECT_GE(asked_at[i] - asked_at[i - 1], 1.0 - 1e-6) << "attempt " << i + 1;
  }
  EXPECT_EQ(old_panel->last_state(), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_EQ(old_panel->close_calls(), 0u);

  // One Warning for the size change and one for the end of the attempts.
  const auto * ended = h.diagnostics.find("still visible after 4 attempts to hide it");
  ASSERT_NE(ended, nullptr);
  EXPECT_EQ(ended->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(ended->panel_key, "p");
  EXPECT_EQ(
    ended->message,
    "'p' the old QarOS panel (stream " + old_panel->stream_id_text() +
    ") that a size change replaced is still visible after 4 attempts to hide it; it stays "
    "visible and is kept until the session ends");
  EXPECT_EQ(h.diagnostics.count("still visible after"), 1u);
  EXPECT_EQ(h.diagnostics.count("is hidden now"), 0u);
  EXPECT_EQ(count_severity(h.diagnostics, DiagnosticSeverity::Warning), 2u);

  // With a sender that cannot detach (the test sender's default), closed once at the end of
  // the session, as every kept panel is.
  h.manager->clear();
  EXPECT_EQ(old_panel->close_calls(), 1u);
  EXPECT_EQ(old_panel->close_steps(), kFullClose);
}

TEST(PanelParkResize, ARevivalOnAWindowOfAnotherSizeKeepsTheOldPanelHiddenToo)
{
  Harness h;
  SourceWidget first(200, 120);
  SourceWidget second(400, 240);
  ASSERT_TRUE(h.manager->add_panel("p", first.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);
  ASSERT_TRUE(h.manager->close_panel("p", "closed").ok());
  h.tick();
  const auto old_panel = h.factory->created()[0];
  ASSERT_EQ(old_panel->states(), kHiddenOnly);

  // The same key on a window of another size: the old panel is shown for a moment (the revival)
  // and then replaced like any other; none of it closes anything.
  ASSERT_TRUE(h.manager->add_panel("p", second.get(), options_no_pacing()).ok());
  for (int i = 0; i < 8; ++i) {
    h.tick(0.2);
  }
  ASSERT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(
    old_panel->states(),
    (std::vector<QarGuiPanelState>{
      QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_VISIBLE, QAR_GUI_PANEL_STATE_HIDDEN}));
  EXPECT_EQ(old_panel->close_calls(), 0u);
  EXPECT_EQ(h.factory->created()[1]->desc().buffer_width, 400u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(h.factory->events(), (std::vector<std::string>{"create p", "create p"}));
}

TEST(PanelParkResize, AStreamWhoseNewSenderCouldNotBeMadeStillHoldsItsKeptPanelsWhenParked)
{
  Harness h;
  SourceWidget source(200, 120);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);

  // The old panel is hidden and kept, then the factory cannot make the new one.
  h.factory->set_fails(true, "no session");
  resize_and_settle(h, source, 400, 240);
  ASSERT_EQ(h.factory->created().size(), 1u) << "no second panel exists";
  EXPECT_GE(h.factory->create_calls(), 2u) << "the new sender was asked for and refused";
  const auto old_panel = h.factory->created()[0];
  EXPECT_EQ(old_panel->states(), kHiddenOnly);
  EXPECT_EQ(old_panel->close_calls(), 0u);
  EXPECT_EQ(h.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 0u) << "said when the new panel exists";

  // Parking such a stream is a park, not "nothing was ever created": the kept panel is not closed.
  ASSERT_TRUE(h.manager->close_panel("p", "closed").ok());
  EXPECT_TRUE(h.manager->has_panel("p"));
  EXPECT_TRUE(h.manager->is_parked("p"));
  EXPECT_EQ(old_panel->close_calls(), 0u);

  // The factory works again, and showing the window again makes the new panel.
  h.factory->set_fails(false);
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  for (int i = 0; i < 6; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.factory->created().size(), 2u);
  EXPECT_EQ(old_panel->close_calls(), 0u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 1u);
  h.manager->clear();
  for (const auto & adapter : h.factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
  }
}

// --- the policy of one stream, without a manager ---------------------------------

TEST(PanelStreamReplacedPanels, WithoutAPolicyARebuildClosesTheOldPanel)
{
  SourceWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  qar_qt_streaming::QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));

  ASSERT_EQ(factory->create_calls(), 2u);
  EXPECT_EQ(factory->created()[0]->close_calls(), 1u);
  EXPECT_EQ(factory->created()[0]->state_calls(), 0u);
  EXPECT_EQ(stream.replaced_panel_count(), 0u);
  qar_qt_streaming::SizeRebuildReport report;
  ASSERT_TRUE(stream.take_size_rebuild(report));
  EXPECT_EQ(report.fate, qar_qt_streaming::ReplacedPanelFate::Closed);
  EXPECT_FALSE(stream.take_size_rebuild(report)) << "the record is taken once";
}

TEST(PanelStreamReplacedPanels, ThePolicyIsAskedOncePerRebuildAndOnlyForARebuild)
{
  SourceWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  qar_qt_streaming::QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  int asked = 0;
  bool keep = true;
  stream.set_replaced_panel_policy([&asked, &keep]() {
      ++asked;
      return keep;
    });
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(asked, 0) << "the first sender replaces nothing";
  qar_qt_streaming::SizeRebuildReport report;
  EXPECT_FALSE(stream.take_size_rebuild(report));

  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));
  EXPECT_EQ(asked, 1);
  ASSERT_TRUE(stream.take_size_rebuild(report));
  EXPECT_EQ(report.fate, qar_qt_streaming::ReplacedPanelFate::HiddenKept);
  EXPECT_TRUE(report.hide_error.empty());
  EXPECT_EQ(stream.replaced_panel_count(), 1u);
  EXPECT_TRUE(stream.has_adapter());

  // The answer is the policy's at the time of each rebuild.
  keep = false;
  source.get()->resize(300, 200);
  QCoreApplication::processEvents();
  stream.tick(2.0);
  stream.tick(2.3);
  ASSERT_TRUE(stream.tick(2.6));
  EXPECT_EQ(asked, 2);
  ASSERT_TRUE(stream.take_size_rebuild(report));
  EXPECT_EQ(report.fate, qar_qt_streaming::ReplacedPanelFate::Closed);
  EXPECT_EQ(stream.replaced_panel_count(), 1u);
  EXPECT_EQ(factory->created()[0]->close_calls(), 0u);
  EXPECT_EQ(factory->created()[1]->close_calls(), 1u);

  // close() closes the current panel and the kept one, once each, however often it is called.
  ASSERT_EQ(factory->created().size(), 3u);
  stream.close();
  stream.close();
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
    EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  }
  EXPECT_EQ(stream.replaced_panel_count(), 0u);
  EXPECT_FALSE(stream.has_adapter());
}

// --- the dialog pose hook -----------------------------------------------------

TEST(DialogPoseHook, TheAdjusterSeesEveryDialogPoseWithItsHeightAndItsAnswerIsUsed)
{
  for (const PanelCloseMode mode : {PanelCloseMode::Park, PanelCloseMode::Notice}) {
    DialogGuard guard;
    Harness h(mode, /*dialogs=*/true);
    ButtonPanel panel;
    PanelStreamOptions owner_options = options_no_pacing();
    owner_options.placement.pose.position = QarVector3{0.3f, 1.0f, -0.5f};
    ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), owner_options).ok());
    h.tick();

    std::vector<QarPose> poses;
    std::vector<float> heights;
    h.manager->set_dialog_pose_adjuster([&](const QarPose & top_centre, float height_m) {
        poses.push_back(top_centre);
        heights.push_back(height_m);
        QarPose raised = top_centre;
        raised.position.y += 2.0f;
        return raised;
      });

    // Promotion: the owner's pose stepped sideways is what the adjuster gets; its answer is
    // the pose of the panel.
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    h.tick();
    ASSERT_EQ(poses.size(), 1u) << to_string(mode);
    EXPECT_FLOAT_EQ(poses[0].position.x, 0.3f + 0.5f);
    EXPECT_FLOAT_EQ(poses[0].position.y, 1.0f);
    EXPECT_FLOAT_EQ(poses[0].position.z, -0.5f);
    // The height of the video of a 240 x 100 px dialog at the width of its widget (100 px *
    // 1.2 mm): a dialog keeps it and is made narrower by the frame instead.
    EXPECT_NEAR(heights[0], 100 * 0.0012f, 1e-5f);
    EXPECT_NEAR(heights[0], h.panel("dialog:Add").height_meters, 1e-5f)
      << "the height the adjuster got is the one the panel has";
    const auto adapter = h.adapter("dialog:Add");
    ASSERT_NE(adapter, nullptr);
    EXPECT_FLOAT_EQ(adapter->desc().placement.pose.position.x, 0.8f);
    EXPECT_FLOAT_EQ(adapter->desc().placement.pose.position.y, 3.0f);

    // Revival: the dialog is dismissed and opened again, and the adjusted pose goes to the
    // revived slot as well.
    dialog->hide();
    QCoreApplication::processEvents();
    h.tick();
    delete dialog;
    QCoreApplication::processEvents();
    QDialog * again = show_dialog(panel.widget(), QSize(240, 100), "Add");
    h.tick();
    ASSERT_EQ(poses.size(), 2u) << to_string(mode);
    EXPECT_FLOAT_EQ(poses[1].position.x, 0.8f);
    EXPECT_EQ(h.factory->create_calls(), 2u) << "the slot was revived, not minted";
    EXPECT_FLOAT_EQ(adapter->placement().pose.position.y, 3.0f);
    EXPECT_FLOAT_EQ(adapter->placement().pose.position.x, 0.8f);

    again->hide();
    QCoreApplication::processEvents();
    delete again;
    QCoreApplication::processEvents();
  }
}

TEST(DialogFrame, TheSweepFitsTheDialogPanelToTheFrameWhileItsOwnerKeepsItsWidth)
{
  const auto ends_with = [](const std::string & text, const std::string & tail) {
      return text.size() >= tail.size() &&
             text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
    };
  for (const PanelCloseMode mode : {PanelCloseMode::Park, PanelCloseMode::Notice}) {
    DialogGuard guard;
    Harness h(mode, /*dialogs=*/true);
    ButtonPanel panel;
    ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    h.tick();

    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 200), "Add");
    h.tick();
    const auto owner = h.panel("p");
    const auto child = h.panel("dialog:Add");
    ASSERT_EQ(child.key, "dialog:Add") << to_string(mode);

    // The dock-like panel, 200 x 120 px and 0.24 m wide: the video the manager's default frame
    // leaves, 0.234 m wide inside the borders and 0.1404 m tall. The dialog, 240 x 200 px: 0.288 m
    // wide with a video of 0.24 m at that width. It keeps the 0.24 m of height, the area inside
    // the frame less the fit margin is 0.24 - 0.069 - 0.003 = 0.168 m tall, and a video of that
    // shape fills it at 0.168 * 1.2 = 0.2016 m, so the panel is 0.2016 + 0.006 = 0.2076 m wide;
    // the line says which rule applied.
    EXPECT_TRUE(
      ends_with(
        owner.last_geometry_summary,
        " (video 0.234000 x 0.140400 m inside a frame of top 0.066000, bottom 0.003000, "
        "left 0.003000, right 0.003000 m, fit margin 0.003000 m)"))
      << owner.last_geometry_summary;
    EXPECT_TRUE(
      ends_with(
        child.last_geometry_summary,
        " (video 0.201600 x 0.168000 m inside a frame of top 0.066000, bottom 0.003000, "
        "left 0.003000, right 0.003000 m, fit margin 0.003000 m "
        "(a dialog: width 0.288000 -> 0.207600 m for the frame))"))
      << child.last_geometry_summary;
    EXPECT_NEAR(child.height_meters, 200 * 0.0012f, 1e-5f);
    EXPECT_NEAR(child.width_meters, 0.2076f, 1e-5f);
    const auto adapter = h.adapter("dialog:Add");
    ASSERT_NE(adapter, nullptr);
    EXPECT_NEAR(adapter->desc().placement.size.height_meters, 200 * 0.0012f, 1e-5f);
    EXPECT_NEAR(adapter->desc().placement.size.width_meters, 0.2076f, 1e-5f);

    dialog->hide();
    QCoreApplication::processEvents();
    h.tick();
    delete dialog;
    QCoreApplication::processEvents();
  }
}

TEST(DialogPoseHook, WithoutAnAdjusterThePosesAreTheOwnersSteppedSideways)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true);
  ButtonPanel panel;
  PanelStreamOptions owner_options = options_no_pacing();
  owner_options.placement.pose.position = QarVector3{0.3f, 1.0f, -0.5f};
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), owner_options).ok());
  h.tick();

  // Set, then cleared again with an empty function: the poses are left alone.
  int calls = 0;
  h.manager->set_dialog_pose_adjuster([&](const QarPose & top_centre, float) {
      ++calls;
      return top_centre;
    });
  h.manager->set_dialog_pose_adjuster(nullptr);

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  h.tick();
  const auto adapter = h.adapter("dialog:Add");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(calls, 0);
  EXPECT_FLOAT_EQ(adapter->desc().placement.pose.position.x, 0.8f);
  EXPECT_FLOAT_EQ(adapter->desc().placement.pose.position.y, 1.0f);
  EXPECT_FLOAT_EQ(adapter->desc().placement.pose.position.z, -0.5f);

  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
}

// --- the dialog height factor ---------------------------------------------------

TEST(DialogHeightFactor, ADialogIsMadeTallerOnceBeforeItsPanelIsMadeAndNeverRebuiltForIt)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true, /*cap=*/16, /*dialog_height_factor=*/1.05);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.tick();

  QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
  ASSERT_EQ(dialog->height(), 100);
  h.tick();  // the sweep promotes the dialog
  EXPECT_EQ(dialog->width(), 240) << "the width is left alone";
  EXPECT_EQ(dialog->height(), 105) << "5 % taller, on the desktop";
  const auto adapter = h.adapter("dialog:Add");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->desc().content_width, 240u);
  EXPECT_EQ(adapter->desc().content_height, 105u) << "the QarOS panel is made at the final height";

  for (int i = 0; i < 8; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.factory->create_calls(), 2u) << "the owner's panel and the dialog's, nothing rebuilt";
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  EXPECT_EQ(adapter->close_calls(), 0u);

  // Dismissed and shown again: the same window is not made taller a second time.
  dialog->hide();
  QCoreApplication::processEvents();
  h.tick();
  ASSERT_TRUE(h.manager->is_parked("dialog:Add"));
  dialog->show();
  QCoreApplication::processEvents();
  h.tick();
  EXPECT_FALSE(h.manager->is_parked("dialog:Add")) << "the slot is revived";
  EXPECT_EQ(dialog->height(), 105) << "made taller once for this window, not again";
  for (int i = 0; i < 8; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);

  // Another window is made taller on its own account.
  QDialog * other = show_dialog(panel.widget(), QSize(300, 200), "Other");
  h.tick();
  EXPECT_EQ(other->width(), 300);
  EXPECT_EQ(other->height(), 210);
  ASSERT_NE(h.adapter("dialog:Other"), nullptr);
  EXPECT_EQ(h.adapter("dialog:Other")->desc().content_height, 210u);

  other->hide();
  dialog->hide();
  QCoreApplication::processEvents();
  delete other;
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogHeightFactor, ANewWindowOfTheSameTitleIsMadeTallerAndRevivesTheSlotWithoutARebuild)
{
  DialogGuard guard;
  Harness h(PanelCloseMode::Park, /*dialogs=*/true, /*cap=*/16, /*dialog_height_factor=*/1.05);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.tick();

  for (int round = 0; round < 3; ++round) {
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    h.tick();
    EXPECT_EQ(dialog->height(), 105) << "round " << round;
    for (int i = 0; i < 6; ++i) {
      h.tick(0.2);
    }
    dialog->hide();
    QCoreApplication::processEvents();
    h.tick();
    delete dialog;
    QCoreApplication::processEvents();
  }
  // Three windows, one sender: each came up at the height the slot was built for.
  EXPECT_EQ(h.factory->create_calls(), 2u);
  EXPECT_EQ(h.panel("dialog:Add").adapter_rebuilds, 1u);
  EXPECT_EQ(h.adapter("dialog:Add")->close_calls(), 0u);
  EXPECT_EQ(h.diagnostics.count("changed size"), 0u);
}

TEST(DialogHeightFactor, WithoutAFactorAboveOneTheDialogKeepsItsHeight)
{
  for (const double factor : {1.0, 0.5, 0.0}) {
    DialogGuard guard;
    Harness h(PanelCloseMode::Park, /*dialogs=*/true, /*cap=*/16, factor);
    ButtonPanel panel;
    ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    h.tick();
    QDialog * dialog = show_dialog(panel.widget(), QSize(240, 100), "Add");
    h.tick();
    EXPECT_EQ(dialog->height(), 100) << "factor " << factor;
    EXPECT_EQ(dialog->width(), 240) << "factor " << factor;
    ASSERT_NE(h.adapter("dialog:Add"), nullptr);
    EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height, 100u) << "factor " << factor;
    dialog->hide();
    QCoreApplication::processEvents();
    delete dialog;
    QCoreApplication::processEvents();
  }
}

// --- a dialog that the cap refuses ---------------------------------------------

namespace
{

const char * const kDialogRefusal =
  "could not give the dialog a panel of its own: the tombstone cap is reached; no new panel "
  "stream can be created this session";

/// A harness whose cap of one parked panel is used up by the panel "other", with the panel "p"
/// still live to own dialogs; dialogs are made 5 % taller.
struct FullCap
{
  FullCap()
  : h(PanelCloseMode::Park, /*dialogs=*/true, /*cap=*/1, /*dialog_height_factor=*/1.05),
    other(200, 120)
  {
    EXPECT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    EXPECT_TRUE(h.manager->add_panel("other", other.get(), options_no_pacing()).ok());
    h.tick();
    EXPECT_TRUE(h.manager->close_panel("other", "closed").ok());
    h.tick();
    EXPECT_TRUE(h.manager->is_parked("other"));
  }

  DialogGuard guard;
  Harness h;
  ButtonPanel panel;
  SourceWidget other;
};

}  // namespace

TEST(DialogCapRefusal, ARefusedDialogIsTriedOnceAndIsNotMadeTaller)
{
  FullCap f;
  Harness & h = f.h;
  QDialog * dialog = show_dialog(f.panel.widget(), QSize(240, 100), "Add");
  h.tick();  // the sweep tries the dialog, and the cap refuses it

  const auto * said = h.diagnostics.find(kDialogRefusal);
  ASSERT_NE(said, nullptr);
  EXPECT_EQ(said->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(said->panel_key, "dialog:Add");
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 1u);
  EXPECT_EQ(dialog->height(), 100) << "a dialog that gets no panel is left as it is";
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 1u);
  EXPECT_EQ(h.factory->create_calls(), 2u);

  // Not asked about again on every tick: one Warning, one refusal, whatever the number of ticks.
  for (int i = 0; i < 12; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 1u);
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 1u);
  EXPECT_EQ(dialog->height(), 100);
  EXPECT_FALSE(h.manager->has_panel("dialog:Add"));

  // A slot is free again: the dialog that is still on show gets its panel, at the taller size.
  SourceWidget again(200, 120);
  ASSERT_TRUE(h.manager->add_panel("other", again.get(), options_no_pacing()).ok());
  h.tick();
  EXPECT_TRUE(h.manager->has_panel("dialog:Add"));
  EXPECT_EQ(dialog->height(), 105) << "made taller once the cap let it through";
  ASSERT_NE(h.adapter("dialog:Add"), nullptr);
  EXPECT_EQ(h.adapter("dialog:Add")->desc().content_height, 105u);
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 1u);

  dialog->hide();
  QCoreApplication::processEvents();
  h.tick();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogCapRefusal, ADialogShownAgainAfterItWasHiddenIsTriedAgain)
{
  FullCap f;
  Harness & h = f.h;
  QDialog * dialog = show_dialog(f.panel.widget(), QSize(240, 100), "Add");
  h.tick();
  ASSERT_EQ(h.diagnostics.count(kDialogRefusal), 1u);

  // Hidden, then shown again: a new opening, so one more try and one more Warning.
  dialog->hide();
  QCoreApplication::processEvents();
  h.tick();
  dialog->show();
  QCoreApplication::processEvents();
  h.tick();
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 2u);
  EXPECT_EQ(h.manager->diagnostics().tombstone_cap_refusals, 2u);
  for (int i = 0; i < 6; ++i) {
    h.tick(0.2);
  }
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 2u) << "and not again on every tick";
  EXPECT_EQ(dialog->height(), 100);

  // A window of its own is its own case: a second dialog is tried and refused on its own account.
  QDialog * second = show_dialog(f.panel.widget(), QSize(300, 200), "Other");
  h.tick();
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 3u);
  EXPECT_EQ(second->height(), 200);

  second->hide();
  dialog->hide();
  QCoreApplication::processEvents();
  h.tick();
  delete second;
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(DialogCapRefusal, ADestroyedRefusedDialogLeavesNothingBehind)
{
  FullCap f;
  Harness & h = f.h;
  QDialog * dialog = show_dialog(f.panel.widget(), QSize(240, 100), "Add");
  h.tick();
  ASSERT_EQ(h.diagnostics.count(kDialogRefusal), 1u);
  dialog->hide();
  QCoreApplication::processEvents();
  delete dialog;
  QCoreApplication::processEvents();
  h.tick();

  // A new window, which may even sit where the old one was: it is tried on its own.
  QDialog * next = show_dialog(f.panel.widget(), QSize(240, 100), "Add");
  h.tick();
  EXPECT_EQ(h.diagnostics.count(kDialogRefusal), 2u);
  next->hide();
  QCoreApplication::processEvents();
  delete next;
  QCoreApplication::processEvents();
}

// --- the burst after an unserved streak ----------------------------------------

TEST(PanelBurst, AStreakOfTenUnservedTicksEarnsThreeUnchangedFramesInARow)
{
  Harness h(PanelCloseMode::Destroy);
  h.factory->set_senders_start_not_ready(true);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_with_keepalive()).ok());
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(h.tick(), 0u);
  }
  const auto adapter = h.adapter("p");
  ASSERT_NE(adapter, nullptr);
  ASSERT_EQ(h.panel("p").not_ready_streak, 10u);
  ASSERT_EQ(adapter->submit_calls(), 0u);

  // QarOS has a destination again. The frame that ends the streak and the next two are sent
  // although nothing on the widget changed; after that the panel is idle again.
  adapter->set_ready(true);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(adapter->submit_calls(), 3u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(adapter->submit_calls(), 3u);
  // The keepalive carries on as before, one second after the last frame.
  EXPECT_EQ(h.tick(1.0), 1u);
  EXPECT_EQ(adapter->submit_calls(), 4u);
}

TEST(PanelBurst, AShorterStreakEarnsNone)
{
  Harness h(PanelCloseMode::Destroy);
  h.factory->set_senders_start_not_ready(true);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_with_keepalive()).ok());
  for (int i = 0; i < 9; ++i) {
    h.tick();
  }
  const auto adapter = h.adapter("p");
  ASSERT_EQ(h.panel("p").not_ready_streak, 9u);

  adapter->set_ready(true);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(adapter->submit_calls(), 1u);
}

TEST(PanelBurst, ABurstWaitsForServiceableFrames)
{
  Harness h(PanelCloseMode::Destroy);
  h.factory->set_senders_start_not_ready(true);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_with_keepalive()).ok());
  for (int i = 0; i < 12; ++i) {
    h.tick();
  }
  const auto adapter = h.adapter("p");
  adapter->set_ready(true);
  ASSERT_EQ(h.tick(), 1u);

  // QarOS has no destination for two ticks: the burst is not spent on them.
  adapter->set_ready(false);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(h.tick(), 0u);
  adapter->set_ready(true);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.tick(), 0u);
  EXPECT_EQ(adapter->submit_calls(), 3u);
}

// --- the luminance lines -------------------------------------------------------

TEST(PanelLuminance, TheFirstFrameIsReportedWithItsMeanLuminanceOnce)
{
  Harness h(PanelCloseMode::Destroy);
  SolidWidget source(QColor(200, 100, 50));
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(), 1u);

  // 0.299 * 200 + 0.587 * 100 + 0.114 * 50 = 124.2
  const auto * line = h.diagnostics.find("frame luminance");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->message, "'p' frame luminance 124.2 (first frame)");
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(line->panel_key, "p");

  for (int i = 0; i < 4; ++i) {
    h.tick();
  }
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 1u) << "once, not once per frame";
}

TEST(PanelLuminance, ABlackFrameAndAWhiteFrameReadAsTheirExtremes)
{
  {
    Harness h(PanelCloseMode::Destroy);
    SolidWidget source(QColor(0, 0, 0));
    ASSERT_TRUE(h.manager->add_panel("black", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    EXPECT_NE(h.diagnostics.find("'black' frame luminance 0.0 (first frame)"), nullptr);
  }
  {
    Harness h(PanelCloseMode::Destroy);
    SolidWidget source(QColor(255, 255, 255));
    ASSERT_TRUE(h.manager->add_panel("white", source.get(), options_no_pacing()).ok());
    ASSERT_EQ(h.tick(), 1u);
    EXPECT_NE(h.diagnostics.find("'white' frame luminance 255.0 (first frame)"), nullptr);
  }
}

TEST(PanelLuminance, EachLongStreakEarnsOneLineAndAShortOneNone)
{
  Harness h(PanelCloseMode::Destroy);
  h.factory->set_senders_start_not_ready(true);
  SolidWidget source(QColor(200, 100, 50));
  ASSERT_TRUE(h.manager->add_panel("p", source.get(), options_with_keepalive()).ok());
  for (int i = 0; i < 12; ++i) {
    h.tick();
  }
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 0u) << "nothing was submitted yet";

  // The very first frame of the stream also ends a streak: both lines, one each.
  const auto adapter = h.adapter("p");
  adapter->set_ready(true);
  ASSERT_EQ(h.tick(), 1u);
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 2u);
  EXPECT_NE(h.diagnostics.find("'p' frame luminance 124.2 (first frame)"), nullptr);
  EXPECT_NE(
    h.diagnostics.find("'p' frame luminance 124.2 (first frame after 12 unserved ticks)"),
    nullptr);
  h.tick();
  h.tick();
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 2u) << "the burst frames say nothing";

  // A second long streak (a tick every 1.5 s, so every tick is a keepalive attempt): one line.
  adapter->set_ready(false);
  for (int i = 0; i < 14; ++i) {
    h.tick(1.5);
  }
  adapter->set_ready(true);
  ASSERT_EQ(h.tick(1.5), 1u);
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 3u);
  EXPECT_NE(
    h.diagnostics.find("(first frame after 14 unserved ticks)"), nullptr);

  // A short one: no line.
  h.tick();
  h.tick();
  h.tick();
  adapter->set_ready(false);
  for (int i = 0; i < 4; ++i) {
    h.tick(1.5);
  }
  adapter->set_ready(true);
  ASSERT_EQ(h.tick(1.5), 1u);
  EXPECT_EQ(h.diagnostics.count("frame luminance"), 3u);
}

// --- the luminance function ----------------------------------------------------

namespace
{

/// A tightly padded R8G8B8A8 frame of one colour.
struct Frame
{
  Frame(std::uint32_t w, std::uint32_t h, std::uint32_t stride_bytes_in, std::uint8_t r,
    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
  : width(w), height(h), stride_bytes(stride_bytes_in),
    bytes(static_cast<std::size_t>(stride_bytes_in) * h, 0)
  {
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        set(x, y, r, g, b, a);
      }
    }
  }

  void set(std::uint32_t x, std::uint32_t y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
    std::uint8_t a = 255)
  {
    std::uint8_t * p = bytes.data() + static_cast<std::size_t>(y) * stride_bytes + x * 4u;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
  }

  qar_ar_core::PanelPixels pixels() const
  {
    qar_ar_core::PanelPixels out;
    out.data = bytes.data();
    out.width = width;
    out.height = height;
    out.stride_bytes = stride_bytes;
    return out;
  }

  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t stride_bytes;
  std::vector<std::uint8_t> bytes;
};

}  // namespace

TEST(FrameLuminance, TheCoefficientsAreTheRec601OnesAndAlphaIsIgnored)
{
  using qar_qt_streaming::mean_luminance;
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 255, 0, 0).pixels()), 76.245, 1e-9);
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 0, 255, 0).pixels()), 149.685, 1e-9);
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 0, 0, 255).pixels()), 29.07, 1e-9);
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 255, 255, 255).pixels()), 255.0, 1e-9);
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 0, 0, 0).pixels()), 0.0, 1e-12);
  EXPECT_NEAR(mean_luminance(Frame(16, 16, 64, 200, 100, 50, 0).pixels()), 124.2, 1e-9)
    << "a transparent pixel counts by its colour";
}

TEST(FrameLuminance, OnlyEveryEighthPixelInXAndYIsRead)
{
  using qar_qt_streaming::mean_luminance;
  // Black, with white at exactly the pixels a step of 8 reads: (0,0), (8,0), (0,8), (8,8).
  Frame frame(16, 16, 64, 0, 0, 0);
  for (const std::uint32_t y : {0u, 8u}) {
    for (const std::uint32_t x : {0u, 8u}) {
      frame.set(x, y, 255, 255, 255);
    }
  }
  EXPECT_NEAR(mean_luminance(frame.pixels()), 255.0, 1e-9);
  EXPECT_NEAR(mean_luminance(frame.pixels(), 8), 255.0, 1e-9);
  // Every pixel: 4 white ones of 256.
  EXPECT_NEAR(mean_luminance(frame.pixels(), 1), 255.0 * 4.0 / 256.0, 1e-9);
  // A pixel the grid skips changes nothing.
  frame.set(3, 5, 255, 255, 255);
  EXPECT_NEAR(mean_luminance(frame.pixels()), 255.0, 1e-9);
}

TEST(FrameLuminance, RowsAreStrideBytesApartAndThePaddingIsNeverRead)
{
  using qar_qt_streaming::mean_luminance;
  // 10 pixels wide, rows 64 bytes apart: the 24 bytes of padding on each row are white and
  // must not count.
  Frame frame(10, 10, 64, 100, 100, 100);
  for (std::uint32_t y = 0; y < 10; ++y) {
    for (std::uint32_t i = 40; i < 64; ++i) {
      frame.bytes[static_cast<std::size_t>(y) * 64u + i] = 255;
    }
  }
  EXPECT_NEAR(mean_luminance(frame.pixels(), 1), 100.0, 1e-9);
  EXPECT_NEAR(mean_luminance(frame.pixels(), 2), 100.0, 1e-9);
}

TEST(FrameLuminance, AFrameWithoutDataOrSizeReadsZero)
{
  using qar_qt_streaming::mean_luminance;
  EXPECT_EQ(mean_luminance(qar_ar_core::PanelPixels{}), 0.0);
  Frame frame(16, 16, 64, 255, 255, 255);
  qar_ar_core::PanelPixels no_width = frame.pixels();
  no_width.width = 0;
  EXPECT_EQ(mean_luminance(no_width), 0.0);
  qar_ar_core::PanelPixels no_height = frame.pixels();
  no_height.height = 0;
  EXPECT_EQ(mean_luminance(no_height), 0.0);
  EXPECT_EQ(mean_luminance(frame.pixels(), 0), 0.0) << "a step of zero reads nothing";
  // A frame smaller than the step still reads its first pixel.
  EXPECT_NEAR(mean_luminance(Frame(3, 3, 12, 255, 255, 255).pixels()), 255.0, 1e-9);
}
