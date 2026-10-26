// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// PanelCloseMode::Destroy — the default (the current QarOS runtime survives
// destroying a stream mid-session): when a streamed Qt window closes, its
// AR panel is closed, its video sender destroyed, and the stream forgotten.
//
// MockQarPanelSenderAdapter models the real adapter's three steps and records
// them (close_steps()), so what is asserted here is what the MANAGER owes: it
// closes each panel exactly once, tells the adapter whether the operator already closed
// it in AR, drops the entry, logs the close, and survives being asked to close
// from inside the panel's own click handler. The ORDER of the real adapter's
// steps has no automated test: it needs the real SDK, so it is checked by running
// the adapter against it.
//
// The tombstone behaviour (PanelCloseMode::Notice) has its own tests in
// panel_tombstone_tests.cpp; the few Notice tests at the bottom of this file
// are about the switch itself.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using qar_ar_core::DiagnosticSeverity;
using qar_qt_streaming::PanelCloseMode;
using qar_ar_core::PanelPointerSample;
using qar_qt_streaming::PanelStreamOptions;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::QueuedPanelInputSource;
using qar_ar_core::Status;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// A streamable widget: a solid colour block, shown under the offscreen platform
/// plugin so render() draws its children. Same recipe as panel_tombstone_tests.cpp.
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
    button_ = new QPushButton("Stop", widget_);
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

/// Asks the manager to close @p key again from inside whatever release/leave/cancel
/// event the panel's widget is sent — which is what a widget whose handler closes
/// its own panel does when the close itself lets go of a pressed pointer.
class AsksToCloseOnAnyReleaseEvent : public QObject
{
public:
  AsksToCloseOnAnyReleaseEvent(QtPanelStreamManager & manager, std::string key)
  : manager_(manager),
    key_(std::move(key))
  {
  }

  void arm() { armed_ = true; }
  int asks() const { return asks_; }

  bool eventFilter(QObject *, QEvent * event) override
  {
    if (!armed_) {
      return false;
    }
    switch (event->type()) {
      case QEvent::MouseButtonRelease:
      case QEvent::Leave:
      case QEvent::HoverLeave:
      case QEvent::TouchCancel:
      case QEvent::TouchEnd:
        ++asks_;
        // The answer is "fine" — a repeated close must not fail or start a second
        // teardown; the assertions below say it did neither.
        EXPECT_TRUE(manager_.close_panel(key_, "asked again from inside the release").ok());
        break;
      default:
        break;
    }
    return false;
  }

private:
  QtPanelStreamManager & manager_;
  std::string key_;
  bool armed_ = false;
  int asks_ = 0;
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

/// A manager wired to a MockQarPanelSenderFactory, a manual clock and a recording sink, with
/// dialog promotion off (these tests are about the close, not about dialogs).
/// Destroy is the config's own default, which is the point.
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
    config.diagnostics = &diagnostics;
    config.input_source = queue;
    config.pump_input_in_tick = false;  // the way the RViz plugin runs it
    config.close_mode = mode;
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  std::shared_ptr<QueuedPanelInputSource> queue;
  RecordingDiagnostics diagnostics;
  std::unique_ptr<QtPanelStreamManager> manager;
};

std::uint32_t
total(const MockQarPanelSenderFactory & factory, std::uint32_t (qar_qt_streaming::test_support::MockQarPanelSenderAdapter::*count)() const)
{
  std::uint32_t sum = 0;
  for (const auto & adapter : factory.created()) {
    sum += ((*adapter).*count)();
  }
  return sum;
}

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

const std::vector<std::string> kFullSequence = {"unsubscribe_input", "close_panel", "destroy_sender"};
const std::vector<std::string> kSkippedSequence = {
  "unsubscribe_input", "close_panel_skipped", "destroy_sender"};

}  // namespace

// --- the close itself --------------------------------------------------------

TEST(PanelCloseReport, TheOutcomeWordsAreTheOnesTheLogPrints)
{
  using qar_ar_core::PanelCloseOutcome;
  EXPECT_STREQ(to_string(PanelCloseOutcome::NoPanel), "none");
  EXPECT_STREQ(to_string(PanelCloseOutcome::Closed), "ran");
  EXPECT_STREQ(to_string(PanelCloseOutcome::AlreadyClosedInAr), "skipped (closed in AR)");
  EXPECT_STREQ(to_string(PanelCloseOutcome::Failed), "FAILED");

  // An untouched report says nothing happened, so a caller that never ran a close
  // cannot mistake it for one that did.
  const qar_ar_core::PanelCloseReport empty;
  EXPECT_FALSE(empty.input_unsubscribed);
  EXPECT_EQ(empty.panel, PanelCloseOutcome::NoPanel);
  EXPECT_TRUE(empty.panel_message.empty());
  EXPECT_FALSE(empty.sender_destroyed);
}

TEST(PanelClose, DestroyIsTheDefault)
{
  const QtPanelStreamingConfig config;
  EXPECT_EQ(config.close_mode, PanelCloseMode::Destroy);
  Harness h;
  EXPECT_EQ(h.manager->close_mode(), PanelCloseMode::Destroy);
}

TEST(PanelClose, ClosingAPanelClosesTheGuiPanelAndDestroysTheSenderOnceInOrder)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();
  ASSERT_NE(adapter, nullptr);
  ASSERT_EQ(adapter->close_calls(), 0u) << "nothing closes before the window does";

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  // Exactly once, in the contract's order: input first, then the GUI panel, then
  // the sender the panel consumed.
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(adapter->close_steps(), kFullSequence);
  EXPECT_EQ(adapter->close_panel_calls(), 1u);
  EXPECT_EQ(adapter->close_panel_skipped(), 0u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  EXPECT_FALSE(adapter->last_close_panel_closed_in_ar());

  // And the stream is GONE: no entry, no notice, nothing left to capture.
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_FALSE(h.manager->is_tombstoned("dock"));
  EXPECT_TRUE(h.manager->panel_keys().empty());
  EXPECT_TRUE(h.manager->tombstoned_panel_keys().empty());
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.panel_count, 0u);
  EXPECT_EQ(snapshot.panels_closed, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 0u) << "destroy mode never leaves a notice";
  EXPECT_EQ(snapshot.tombstoned_panels, 0u);
}

TEST(PanelClose, ANoticeIsNeverBuiltAndTheClosedPanelIsNeverCapturedAgain)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();
  const std::size_t frames = adapter->frames().size();

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  for (int i = 0; i < 10; ++i) {
    h.clock->advance(0.1);
    EXPECT_EQ(h.manager->tick(), 0u) << "nothing is left to submit";
  }
  EXPECT_EQ(adapter->frames().size(), frames) << "not even a notice frame";
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u);
}

TEST(PanelClose, ReopeningTheSameWindowIsAFreshAddWithANewSender)
{
  Harness h;
  {
    SourceWidget first;
    ASSERT_TRUE(h.manager->add_panel("dock", first.get(), options_no_pacing()).ok());
    h.clock->advance(0.1);
    ASSERT_EQ(h.manager->tick(), 1u);
    ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  }
  const auto old_adapter = h.factory->latest();

  SourceWidget second;
  ASSERT_TRUE(h.manager->add_panel("dock", second.get(), options_no_pacing()).ok())
    << "the key is free again";
  EXPECT_FALSE(h.manager->is_tombstoned("dock"));
  h.clock->advance(0.1);
  EXPECT_EQ(h.manager->tick(), 1u);

  // A NEW sender, not the old one revived: the old one is gone for good.
  EXPECT_EQ(h.factory->create_calls(), 2u);
  const auto new_adapter = h.factory->latest();
  EXPECT_NE(new_adapter, old_adapter);
  EXPECT_NE(new_adapter->stream_id_text(), old_adapter->stream_id_text());
  EXPECT_EQ(old_adapter->close_calls(), 1u);
  EXPECT_EQ(new_adapter->close_calls(), 0u);
  // The old one was closed BEFORE the new one was created: never two senders for
  // one panel at once.
  EXPECT_EQ(
    h.factory->events(),
    (std::vector<std::string>{"create dock", "close dock", "create dock"}));
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.tombstones_revived, 0u) << "there was nothing to revive";
  EXPECT_EQ(snapshot.panels_closed, 1u);
  EXPECT_EQ(snapshot.panel_count, 1u);
}

TEST(PanelClose, ManyOpenCloseCyclesLeaveNothingBehind)
{
  Harness h;
  for (int cycle = 0; cycle < 25; ++cycle) {
    SourceWidget source;
    ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok())
      << "cycle " << cycle;
    h.clock->advance(0.1);
    ASSERT_EQ(h.manager->tick(), 1u) << "cycle " << cycle;
    ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok()) << "cycle " << cycle;
  }
  // Every cycle minted a sender and every sender was destroyed, exactly once.
  EXPECT_EQ(h.factory->create_calls(), 25u);
  EXPECT_EQ(total(*h.factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_calls), 25u);
  EXPECT_EQ(
    total(*h.factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::sender_destroy_calls), 25u);
  EXPECT_TRUE(h.manager->panel_keys().empty());
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.panels_closed, 25u);
  EXPECT_EQ(snapshot.tombstones_created, 0u);
}

TEST(PanelClose, TheTombstoneCapNoLongerBindsBecauseNothingAccumulates)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  config.dialog_panels = false;
  config.max_tombstoned_panels = 2;
  QtPanelStreamManager manager(config);

  std::vector<std::unique_ptr<SourceWidget>> widgets;
  for (int i = 0; i < 6; ++i) {
    widgets.push_back(std::make_unique<SourceWidget>());
    const std::string key = "dock" + std::to_string(i);
    ASSERT_TRUE(manager.add_panel(key, widgets.back()->get(), options_no_pacing()).ok())
      << "window " << i << " must not be refused: the closed ones are gone, not kept";
    clock->advance(0.1);
    manager.tick();
    ASSERT_TRUE(manager.close_panel(key, "closed in RViz").ok());
  }
  EXPECT_EQ(manager.diagnostics().tombstone_cap_refusals, 0u);
  EXPECT_EQ(factory->create_calls(), 6u);
}

TEST(PanelClose, ClosingAKeyThatIsNotStreamingIsNotFoundAndTouchesNothing)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);

  EXPECT_EQ(h.manager->close_panel("nothing-here", "x").status, Status::NotFound);
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  EXPECT_EQ(h.manager->close_panel("dock", "closed in RViz").status, Status::NotFound)
    << "a panel is closed once";
  EXPECT_EQ(h.factory->latest()->close_calls(), 1u);
}

TEST(PanelClose, ClosingAPanelThatNeverGotASenderJustForgetsIt)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.factory->create_calls(), 0u) << "no tick yet, so no sender";

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_EQ(h.factory->create_calls(), 0u);
  EXPECT_NE(h.diagnostics.find("none to destroy"), nullptr) << "and the log says so";
}

TEST(PanelClose, RemovePanelFollowsTheCloseMode)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);

  ASSERT_TRUE(h.manager->remove_panel("dock").ok());
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_EQ(h.factory->latest()->close_calls(), 1u);
  EXPECT_NE(h.diagnostics.find("none given"), nullptr) << "no reason was passed";
}

// --- the AR side -------------------------------------------------------------

TEST(PanelClose, AnArClosedPanelIsNotClosedAgainButItsSenderIsStillDestroyed)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // The operator closes the panel in AR: capture stops, and nothing else.
  ASSERT_TRUE(h.manager->note_ar_panel_closed("dock"));
  EXPECT_EQ(adapter->close_calls(), 0u) << "the AR close alone destroys nothing";
  EXPECT_TRUE(h.manager->has_panel("dock"));

  // Then the Qt window closes.
  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  EXPECT_TRUE(adapter->last_close_panel_closed_in_ar())
    << "the adapter has to be told, or it closes the panel a second time";
  EXPECT_EQ(adapter->close_panel_calls(), 0u) << "never close_panel twice for one panel";
  EXPECT_EQ(adapter->close_panel_skipped(), 1u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u) << "...but the sender is destroyed";
  EXPECT_EQ(adapter->close_steps(), kSkippedSequence);
  EXPECT_FALSE(h.manager->has_panel("dock"));
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.close_panel_skipped, 1u);
  EXPECT_EQ(snapshot.panels_closed, 1u);
  EXPECT_NE(h.diagnostics.find("close_panel skipped (closed in AR)"), nullptr);
}

TEST(PanelClose, AnArCloseStillWaitingInTheQueueIsAppliedBeforeTheQtClose)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // What the runtime's callback pushes the moment the operator closes the panel —
  // and no tick has drained it yet. The window closes in the same breath.
  h.queue->push_closed_panel("dock");
  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  EXPECT_TRUE(adapter->last_close_panel_closed_in_ar());
  EXPECT_EQ(adapter->close_panel_calls(), 0u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  // And the queue holds nothing stale to be misread, a tick later, as news about
  // whatever takes this key next.
  SourceWidget again;
  ASSERT_TRUE(h.manager->add_panel("dock", again.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  h.clock->advance(0.1);
  h.manager->tick();
  const auto snapshot = h.manager->diagnostics();
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_FALSE(snapshot.panels.front().ar_panel_closed);
  EXPECT_EQ(snapshot.panels.front().frames_captured, 2u)
    << "the new panel is captured on every tick, not suspended by the old one's close";
}

// --- a failure ---------------------------------------------------------------

TEST(PanelClose, AFailedClosePanelStillDestroysTheSenderDropsTheEntryAndWarns)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();
  adapter->fail_next_close_panel();

  const auto closed = h.manager->close_panel("dock", "closed in RViz");
  EXPECT_TRUE(closed.ok()) << "the panel is gone from this side; the failure is reported, not returned";

  // Not resurrected as a notice, and the sender went anyway.
  EXPECT_FALSE(h.manager->has_panel("dock"));
  EXPECT_FALSE(h.manager->is_tombstoned("dock"));
  EXPECT_EQ(adapter->close_panel_calls(), 1u);
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  EXPECT_EQ(adapter->close_steps(), kFullSequence);
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.close_panel_failures, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 0u);

  // ...and logged where it can be seen, with the runtime's own words.
  const auto * warning = h.diagnostics.find("could not be closed");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "dock");
  EXPECT_NE(warning->message.find("mock close_panel failure"), std::string::npos);
  EXPECT_NE(warning->message.find("destroyed"), std::string::npos);
  EXPECT_NE(warning->message.find(adapter->stream_id_text()), std::string::npos);
}

// --- session shutdown --------------------------------------------------------

TEST(PanelClose, SessionShutdownAfterClosesDoubleFreesNothing)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  {
    auto clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = false;
    QtPanelStreamManager manager(config);

    SourceWidget closed_in_qt;
    SourceWidget closed_in_ar;
    SourceWidget still_open;
    ASSERT_TRUE(manager.add_panel("qt", closed_in_qt.get(), options_no_pacing()).ok());
    ASSERT_TRUE(manager.add_panel("ar", closed_in_ar.get(), options_no_pacing()).ok());
    ASSERT_TRUE(manager.add_panel("live", still_open.get(), options_no_pacing()).ok());
    clock->advance(0.1);
    ASSERT_EQ(manager.tick(), 3u);
    ASSERT_EQ(factory->create_calls(), 3u);

    ASSERT_TRUE(manager.close_panel("qt", "closed in RViz").ok());
    ASSERT_TRUE(manager.note_ar_panel_closed("ar"));
    EXPECT_EQ(total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_calls), 1u)
      << "only the Qt close has closed anything so far";

    manager.clear();
    // Every sender destroyed exactly once: "qt" already was, and is not touched
    // again; "ar" and "live" go now.
    EXPECT_EQ(total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_calls), 3u);
    EXPECT_EQ(
      total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::sender_destroy_calls), 3u);
    // close_panel itself: "qt" and "live" ran it; "ar" was already closed by the
    // operator and is skipped at shutdown exactly as it would have been earlier.
    EXPECT_EQ(
      total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_panel_calls), 2u);
    EXPECT_EQ(
      total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_panel_skipped), 1u);
    // ...and the destructor, which runs clear() again, must not close them a
    // second time.
  }
  EXPECT_EQ(total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_calls), 3u);
  EXPECT_EQ(
    total(*factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::sender_destroy_calls), 3u);
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->sender_destroy_calls(), 1u) << adapter->desc().key;
  }
}

TEST(PanelClose, ClearAfterEveryPanelWasClosedHasNothingLeftToDo)
{
  Harness h;
  SourceWidget a;
  SourceWidget b;
  ASSERT_TRUE(h.manager->add_panel("a", a.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("b", b.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 2u);
  ASSERT_TRUE(h.manager->close_panel("a", "closed in RViz").ok());
  ASSERT_TRUE(h.manager->close_panel("b", "closed in RViz").ok());

  h.manager->clear();
  h.manager->clear();
  EXPECT_EQ(total(*h.factory, &qar_qt_streaming::test_support::MockQarPanelSenderAdapter::close_calls), 2u);
}

// --- the log -----------------------------------------------------------------

TEST(PanelClose, EveryCloseIsLoggedWithKeyReasonStepsAndFramesServed)
{
  Harness h;
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock:Displays", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const std::string stream_id = h.factory->latest()->stream_id_text();

  ASSERT_TRUE(h.manager->close_panel("dock:Displays", "closed in RViz").ok());

  const auto * line = h.diagnostics.find("closed 'dock:Displays'");
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->severity, DiagnosticSeverity::Info);
  EXPECT_EQ(line->panel_key, "dock:Displays");
  EXPECT_NE(line->message.find("(stream " + stream_id + ")"), std::string::npos);
  EXPECT_NE(line->message.find("after 1 frame(s)"), std::string::npos);
  EXPECT_NE(line->message.find("reason: closed in RViz"), std::string::npos);
  EXPECT_NE(line->message.find("input subscriptions cancelled"), std::string::npos);
  EXPECT_NE(line->message.find("close_panel ran"), std::string::npos);
  EXPECT_NE(line->message.find("video sender destroyed"), std::string::npos);
  EXPECT_EQ(h.diagnostics.count("closed 'dock:Displays'"), 1u) << "one summary line per close";
}

TEST(PanelClose, ClosingAPanelThatNeverServedAFrameIsAWarningNamingTheStream)
{
  Harness h;
  h.factory->set_senders_start_not_ready(true);
  SourceWidget source;
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options).ok());
  h.manager->tick();
  h.clock->advance(3.0);
  h.manager->tick();
  const std::string stream_id = h.factory->latest()->stream_id_text();

  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  const auto * warning = h.diagnostics.find("having served it NO frames");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, DiagnosticSeverity::Warning);
  EXPECT_NE(warning->message.find(stream_id), std::string::npos);
  EXPECT_EQ(h.diagnostics.count("closed 'dock'"), 1u);
}

// --- closing from inside the panel's own handler ----------------------------

TEST(PanelClose, ACloseAskedForFromThePanelsOwnClickRunsOnceTheClickHasReturned)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // The Stop button, pressed in AR on the very panel it stops: the click is
  // delivered synchronously, so the handler runs INSIDE the stream's own
  // deliver_input, with the injector, the stream and the map entry all on the
  // stack beneath it.
  int handler_runs = 0;
  bool alive_inside_handler = false;
  qar_ar_core::Result asked;
  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      ++handler_runs;
      asked = h.manager->close_panel("p", "stopped in RViz");
      alive_inside_handler = h.manager->has_panel("p");
    });

  EXPECT_TRUE(h.manager->handle_sample(centre_click("p")));

  EXPECT_EQ(handler_runs, 1);
  EXPECT_TRUE(asked.ok()) << "from the caller's side the close succeeded";
  EXPECT_TRUE(alive_inside_handler)
    << "the stream is not destroyed out from under its own handler";
  // ...and by the time the outermost input frame has unwound, it is gone.
  EXPECT_FALSE(h.manager->has_panel("p"));
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(adapter->close_steps(), kFullSequence);
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.closes_deferred, 1u);
  EXPECT_EQ(snapshot.panels_closed, 1u);
  EXPECT_NE(h.diagnostics.find("was asked for from inside a pointer action"), nullptr);
}

TEST(PanelClose, ADeferredCloseAskedForTwiceRunsOnce)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      EXPECT_TRUE(h.manager->close_panel("p", "stopped in RViz").ok());
      EXPECT_TRUE(h.manager->close_panel("p", "stopped in RViz").ok());
      EXPECT_TRUE(h.manager->remove_panel("p").ok());
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_click("p")));

  EXPECT_FALSE(h.manager->has_panel("p"));
  EXPECT_EQ(adapter->close_calls(), 1u);
  EXPECT_EQ(h.manager->diagnostics().closes_deferred, 1u);
  EXPECT_EQ(h.manager->diagnostics().panels_closed, 1u);
}

TEST(PanelClose, ACloseOfAnotherPanelFromAClickHandlerIsImmediate)
{
  Harness h;
  ButtonPanel stopper;     // the panel being clicked
  SourceWidget victim;     // the panel it closes
  ASSERT_TRUE(h.manager->add_panel("stopper", stopper.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("victim", victim.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 2u);

  bool victim_gone_inside_handler = false;
  QObject::connect(
    stopper.button(), &QPushButton::clicked, [&]() {
      EXPECT_TRUE(h.manager->close_panel("victim", "stopped in RViz").ok());
      victim_gone_inside_handler = !h.manager->has_panel("victim");
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_click("stopper")));

  EXPECT_TRUE(victim_gone_inside_handler) << "only the clicked panel has to wait";
  EXPECT_TRUE(h.manager->has_panel("stopper"));
  EXPECT_EQ(h.manager->diagnostics().closes_deferred, 0u);
}

TEST(PanelCloseNoticeMode, ACloseAskedForFromThePanelsOwnClickIsMadeOnceTheClickHasReturned)
{
  Harness h(PanelCloseMode::Notice);
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // The Stop button, pressed in AR on the very panel it stops: the handler runs INSIDE the
  // stream's own deliver_input, with the input injector on the stack beneath it. The notice
  // drops that injector, so it must not be made until the click has returned.
  int handler_runs = 0;
  qar_ar_core::Result asked;
  bool tombstoned_inside_handler = true;
  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      ++handler_runs;
      asked = h.manager->close_panel("p", "stopped in RViz", "This window has been stopped.");
      // Asked again, and by the other door: still one deferred close.
      EXPECT_TRUE(h.manager->close_panel("p", "stopped in RViz").ok());
      EXPECT_TRUE(h.manager->remove_panel("p").ok());
      tombstoned_inside_handler = h.manager->is_tombstoned("p");
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_click("p")));

  EXPECT_EQ(handler_runs, 1);
  EXPECT_TRUE(asked.ok()) << "from the caller's side the close succeeded";
  EXPECT_FALSE(tombstoned_inside_handler)
    << "the stream is not turned into a notice under its own handler";
  // ...and by the time the outermost input frame has unwound, it is the notice.
  EXPECT_TRUE(h.manager->has_panel("p"));
  EXPECT_TRUE(h.manager->is_tombstoned("p"));
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.closes_deferred, 1u);
  EXPECT_EQ(snapshot.tombstones_created, 1u);
  EXPECT_EQ(snapshot.panels_closed, 0u);
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_EQ(snapshot.panels.front().tombstone_reason, "This window has been stopped.")
    << "the deferred close keeps the notice's own wording";
  EXPECT_EQ(adapter->close_calls(), 0u) << "notice mode keeps the sender until shutdown";
  EXPECT_NE(h.diagnostics.find("was asked for from inside a pointer action"), nullptr);

  // The notice takes no input any more.
  EXPECT_FALSE(h.manager->handle_sample(centre_click("p")));
  EXPECT_EQ(handler_runs, 1);
}

TEST(PanelCloseNoticeMode, ACloseOfAnotherPanelFromAClickHandlerIsImmediate)
{
  Harness h(PanelCloseMode::Notice);
  ButtonPanel stopper;     // the panel being clicked
  SourceWidget victim;     // the panel it closes
  ASSERT_TRUE(h.manager->add_panel("stopper", stopper.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("victim", victim.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 2u);

  bool victim_is_a_notice_inside_handler = false;
  QObject::connect(
    stopper.button(), &QPushButton::clicked, [&]() {
      EXPECT_TRUE(h.manager->close_panel("victim", "stopped in RViz").ok());
      victim_is_a_notice_inside_handler = h.manager->is_tombstoned("victim");
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_click("stopper")));

  EXPECT_TRUE(victim_is_a_notice_inside_handler) << "only the clicked panel has to wait";
  EXPECT_FALSE(h.manager->is_tombstoned("stopper"));
  EXPECT_EQ(h.manager->diagnostics().closes_deferred, 0u);
  EXPECT_EQ(h.manager->diagnostics().tombstones_created, 1u);
}

TEST(PanelCloseNoticeMode, ClosingAKeyThatIsNotStreamingIsNotFound)
{
  Harness h(PanelCloseMode::Notice);
  EXPECT_EQ(h.manager->close_panel("nothing-here", "x").status, Status::NotFound);
  EXPECT_EQ(h.manager->remove_panel("nothing-here").status, Status::NotFound);
}

TEST(PanelClose, ATickThatRunsMidDeliveryLeavesTheDeferredCloseAlone)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // What the real panel does when the click opens a nested event loop: the render
  // timer keeps firing INSIDE the handler, and its tick must not destroy the
  // stream the handler is still running in. Modelled by ticking from the handler,
  // before handle_sample has returned.
  bool still_there_after_tick = false;
  QObject::connect(
    panel.button(), &QPushButton::clicked, [&]() {
      ASSERT_TRUE(h.manager->close_panel("p", "stopped in RViz").ok());
      h.manager->tick();  // a timer firing inside a nested loop
      still_there_after_tick = h.manager->has_panel("p");
    });
  EXPECT_TRUE(h.manager->handle_sample(centre_click("p")));

  EXPECT_TRUE(still_there_after_tick)
    << "a tick that runs while the stream is still mid-delivery must leave it alone";
  EXPECT_FALSE(h.manager->has_panel("p")) << "closed on the way out of the outermost frame";
  EXPECT_EQ(adapter->close_calls(), 1u);
}

TEST(PanelClose, ARepeatedCloseAskedForWhileThePointerIsBeingReleasedIsIgnored)
{
  Harness h;
  ButtonPanel panel;
  ASSERT_TRUE(h.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  // Hold the button down, then close the panel: letting go of the pressed pointer
  // delivers events into the button, and a widget is entitled to answer one of
  // them by asking for the close again.
  PanelPointerSample press = centre_click("p");
  press.phase = PointerPhase::Started;
  ASSERT_TRUE(h.manager->handle_sample(press));
  ASSERT_TRUE(panel.button()->isDown());

  AsksToCloseOnAnyReleaseEvent asker(*h.manager, "p");
  panel.button()->installEventFilter(&asker);
  asker.arm();

  ASSERT_TRUE(h.manager->close_panel("p", "closed in RViz").ok());
  panel.button()->removeEventFilter(&asker);

  const int reentrant_asks = asker.asks();
  EXPECT_GE(reentrant_asks, 1) << "the release really did deliver events into the widget";
  EXPECT_FALSE(panel.button()->isDown());
  EXPECT_FALSE(h.manager->has_panel("p"));
  EXPECT_EQ(adapter->close_calls(), 1u) << "closed once, however often it was asked";
  EXPECT_EQ(adapter->sender_destroy_calls(), 1u);
  EXPECT_EQ(h.manager->diagnostics().panels_closed, 1u);
}

// --- the switch --------------------------------------------------------------

TEST(PanelCloseNoticeMode, ClosePanelIsTheTombstoneAndKeepsTheSender)
{
  Harness h(PanelCloseMode::Notice);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  const auto adapter = h.factory->latest();

  ASSERT_TRUE(
    h.manager->close_panel("dock", "closed in RViz", "This window has been closed in RViz.").ok());

  EXPECT_TRUE(h.manager->has_panel("dock"));
  EXPECT_TRUE(h.manager->is_tombstoned("dock"));
  EXPECT_EQ(adapter->close_calls(), 0u) << "notice mode keeps the sender until shutdown";
  const auto snapshot = h.manager->diagnostics();
  EXPECT_EQ(snapshot.tombstones_created, 1u);
  EXPECT_EQ(snapshot.panels_closed, 0u);
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_EQ(snapshot.panels.front().tombstone_reason, "This window has been closed in RViz.")
    << "the notice says what the notice text says, not the log's short phrase";
}

TEST(PanelCloseNoticeMode, WithoutANoticeTextTheReasonIsTheNoticeText)
{
  Harness h(PanelCloseMode::Notice);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);
  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());
  EXPECT_EQ(h.manager->diagnostics().panels.front().tombstone_reason, "closed in RViz");
}

TEST(PanelCloseNoticeMode, RemovePanelTombstonesAndClosingTwiceIsANoOp)
{
  Harness h(PanelCloseMode::Notice);
  SourceWidget source;
  ASSERT_TRUE(h.manager->add_panel("dock", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.manager->tick(), 1u);

  ASSERT_TRUE(h.manager->remove_panel("dock").ok());
  EXPECT_TRUE(h.manager->is_tombstoned("dock"));
  EXPECT_TRUE(h.manager->remove_panel("dock").ok()) << "tombstoning twice is a no-op";
  EXPECT_EQ(h.manager->close_panel("nothing-here", "x").status, Status::NotFound);
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);
}

TEST(PanelCloseNoticeMode, ReopeningRevivesTheSameSenderAsBefore)
{
  Harness h(PanelCloseMode::Notice);
  SourceWidget first;
  ASSERT_TRUE(h.manager->add_panel("dock", first.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  ASSERT_EQ(h.manager->tick(), 1u);
  ASSERT_TRUE(h.manager->close_panel("dock", "closed in RViz").ok());

  SourceWidget second;
  ASSERT_TRUE(h.manager->add_panel("dock", second.get(), options_no_pacing()).ok());
  h.clock->advance(0.1);
  EXPECT_EQ(h.manager->tick(), 1u);
  EXPECT_EQ(h.factory->create_calls(), 1u) << "the escape hatch re-uses the retired slot";
  EXPECT_EQ(h.factory->latest()->close_calls(), 0u);
}
