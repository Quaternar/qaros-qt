// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The per-panel capture/submit cycle and the manager's bookkeeping, driven
// against real widgets (offscreen platform) and the recording
// MockQarPanelSenderAdapter.

#include <gtest/gtest.h>

#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using qar_ar_core::PanelPointerSample;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::PanelPointerEvent;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_ar_core::Status;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordingPanelInputSink;

namespace
{

class Fixture
{
public:
  Fixture()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    // A solid colour block, NOT a text label: the offscreen platform plugin
    // rasterizes no glyphs on this host, so a text change would not alter a
    // single pixel and every dirty-check assertion would be vacuous. A palette
    // fill exercises exactly the same render() path without a font engine.
    block_ = new QWidget(widget_);
    block_->setAutoFillBackground(true);
    set_block_colour(Qt::red);
    layout->addWidget(block_);
    widget_->resize(200, 120);
    // show() is required, not cosmetic: QWidget::render() draws only children
    // that are isVisible(), and visibility is inherited. Under the offscreen
    // platform plugin this creates no window — nothing appears on any desktop.
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~Fixture() { delete widget_; }

  QWidget * widget() const { return widget_; }

  /// Change the rendered content, so the next capture must come out dirty.
  void mutate() { set_block_colour(Qt::blue); }

private:
  void set_block_colour(Qt::GlobalColor colour)
  {
    QPalette palette = block_->palette();
    palette.setColor(QPalette::Window, colour);
    block_->setPalette(palette);
  }

  QWidget * widget_ = nullptr;
  QWidget * block_ = nullptr;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.display_name = "Test Panel";
  options.max_fps = 0;          // no pacing: every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check from the keepalive
  return options;
}

}  // namespace

TEST(PanelStream, FirstTickCreatesTheSenderAndSubmitsAFrame)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  EXPECT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);
  ASSERT_NE(factory->latest(), nullptr);
  EXPECT_EQ(factory->latest()->submit_calls(), 1u);
  EXPECT_EQ(stream.diagnostics().frames_submitted, 1u);
}

TEST(PanelStream, TheSenderIsToldTheDerivedBufferSizeAndPhysicalSize)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const auto & desc = factory->latest()->desc();
  EXPECT_EQ(desc.buffer_width, 200u);
  EXPECT_EQ(desc.buffer_height, 120u);
  EXPECT_EQ(desc.display_name, "Test Panel");
  // 200 logical px * kDefaultMetersPerLogicalPx (0.0012 m/px).
  EXPECT_NEAR(desc.placement.size.width_meters, 0.24f, 1e-4f);
}

TEST(PanelStream, AnUnchangedWidgetIsCapturedButNotSubmitted)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_FALSE(stream.tick(2.0));

  EXPECT_EQ(factory->latest()->submit_calls(), 1u);
  EXPECT_EQ(stream.diagnostics().frames_captured, 3u);
  EXPECT_EQ(stream.diagnostics().frames_skipped_unchanged, 2u);
  // The skipped ticks must not have reserved a send cycle either: a reserved
  // cycle has to be followed by a show_frame.
  EXPECT_EQ(factory->latest()->begin_calls(), 1u);
}

TEST(PanelStream, EditingTheWidgetSubmitsAgain)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_FALSE(stream.tick(1.0));

  fixture.mutate();
  EXPECT_TRUE(stream.tick(2.0));
  EXPECT_EQ(factory->latest()->submit_calls(), 2u);
}

TEST(PanelStream, KeepaliveResendsAnUnchangedFrameOnSchedule)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(0.5));  // unchanged and keepalive not due
  EXPECT_TRUE(stream.tick(1.5));   // unchanged but keepalive due
  EXPECT_EQ(factory->latest()->submit_calls(), 2u);
}

TEST(PanelStream, TheFpsCapShedsTicks)
{
  Fixture fixture;
  PanelStreamOptions options = options_no_pacing();
  options.max_fps = 10;  // one capture per 100 ms

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options, factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(0.05));
  EXPECT_EQ(stream.diagnostics().frames_skipped_paced, 1u);
  // The shed tick must not even have captured — that repaint is the cost the
  // cap exists to avoid.
  EXPECT_EQ(stream.diagnostics().frames_captured, 1u);

  fixture.mutate();
  EXPECT_TRUE(stream.tick(0.2));
}

TEST(PanelStream, NotReadyIsSkippedRatherThanTreatedAsAnError)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  factory->latest()->set_ready(false);
  fixture.mutate();

  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_EQ(stream.diagnostics().frames_skipped_not_ready, 1u);
  EXPECT_TRUE(stream.diagnostics().last_error.empty());
}

TEST(PanelStream, AResizeRebuildsTheSenderBecauseTheLayoutIsFixedAtCreation)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  // A real resize still rebuilds — but only once it has SETTLED, so it is ticked
  // past both gates (>= resize_settle_seconds AND >= resize_settle_ticks). The
  // hysteresis tests below are where that rule is pinned down.
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));

  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_EQ(factory->created().front()->close_calls(), 1u) << "the old sender must be closed";
  EXPECT_EQ(factory->latest()->desc().buffer_width, 320u);
}

// --- Resize hysteresis -----------------------------------------------------
//
// Rebuilding a panel is not a redraw: it retires the click/hover subscription,
// closes the QarOS panel, destroys the video sender and builds all three again
// (and the real input source spends two never-reused callback slots doing it).
// The pathological case is a status label INSIDE the streamed widget that
// grows, which widens the dock, which changes the capture buffer, which
// rebuilds the panel, which rewrites the status label — and the panel spends
// the session rebuilding instead of streaming. These pin the rule that stops it.

TEST(PanelStream, AResizeSmallerThanTheThresholdIsAbsorbedWithoutARebuild)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 1u);

  // 4 px, under the 8 px default threshold: not worth a rebuild however long it
  // is held.
  fixture.widget()->resize(204, 120);
  fixture.widget()->layout()->activate();
  for (int i = 1; i <= 20; ++i) {
    stream.tick(static_cast<double>(i));
  }

  EXPECT_EQ(factory->create_calls(), 1u) << "a few pixels must not rebuild the panel";
  EXPECT_GT(stream.diagnostics().resizes_ignored_small, 0u);
  EXPECT_FALSE(stream.diagnostics().resize_pending);
  // ...and the panel keeps streaming at the layout its live sender was built
  // for, so the captured buffer can never mismatch the sender.
  EXPECT_EQ(stream.diagnostics().buffer_width, 200u);
  EXPECT_EQ(factory->latest()->desc().buffer_width, 200u);
}

TEST(PanelStream, AnOscillatingWidthNeverSettlesSoTheSenderIsNeverRebuilt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  ASSERT_EQ(factory->create_calls(), 1u);

  // The regression itself: the width flips between two SIGNIFICANT sizes every
  // 0.25 s — the rate the status text inside the widget was rewritten. Neither
  // candidate is ever held long enough to be adopted, so nothing is rebuilt,
  // which is the whole point: each rebuild is what rewrote the text that
  // changed the width.
  double now = 0.0;
  for (int i = 0; i < 40; ++i) {
    fixture.widget()->resize((i % 2) == 0 ? 260 : 200, 120);
    fixture.widget()->layout()->activate();
    now += 0.25;
    stream.tick(now);
  }

  EXPECT_EQ(factory->create_calls(), 1u) << "an oscillating size must not churn the sender";
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 1u);
  EXPECT_EQ(factory->created().front()->close_calls(), 0u) << "the live sender stays live";
}

TEST(PanelStream, ASustainedResizeIsAdoptedOnlyOnceBothSettleGatesArePassed)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();

  // Tick 1: the candidate has only just been seen. Neither gate is passed.
  stream.tick(1.0);
  EXPECT_EQ(factory->create_calls(), 1u);
  EXPECT_TRUE(stream.diagnostics().resize_pending);
  EXPECT_EQ(stream.diagnostics().pending_buffer_width, 320u);

  // Enough SECONDS but only two ticks: still not adopted. The tick gate is what
  // stops one stray sample from a slowly ticked panel counting as "held".
  stream.tick(2.0);
  EXPECT_EQ(factory->create_calls(), 1u) << "two ticks is not resize_settle_ticks";

  // Third consecutive tick on the same size, well past the settle seconds.
  ASSERT_TRUE(stream.tick(3.0));
  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_FALSE(stream.diagnostics().resize_pending);
  EXPECT_EQ(stream.diagnostics().adapter_rebuilds, 2u);
  EXPECT_EQ(factory->latest()->desc().buffer_width, 320u);
}

TEST(PanelStream, EnoughTicksButNotEnoughSecondsDoesNotAdoptEither)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();

  // Ten ticks inside 0.1 s — a 60 Hz GUI thread during a splitter drag. The
  // seconds gate is the one that matters there.
  for (int i = 1; i <= 10; ++i) {
    stream.tick(0.01 * static_cast<double>(i));
  }
  EXPECT_EQ(factory->create_calls(), 1u);
  EXPECT_TRUE(stream.diagnostics().resize_pending);

  // Hold the same size past resize_settle_seconds and it goes through.
  ASSERT_TRUE(stream.tick(1.0));
  EXPECT_EQ(factory->create_calls(), 2u);
}

TEST(PanelStream, WhileAResizeSettlesTheSubmittedFrameStillMatchesTheLiveSender)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  auto first = factory->latest();

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  fixture.mutate();
  stream.tick(0.1);

  // The sender's buffer layout is fixed at creation, so a frame captured at the
  // widget's NEW size would be rejected by the real adapter (it compares the
  // sizes before copying). Everything submitted before the resize is adopted
  // must still be the size the live sender was built for.
  ASSERT_FALSE(first->frames().empty());
  for (const auto & frame : first->frames()) {
    EXPECT_EQ(frame.width, first->desc().buffer_width);
    EXPECT_EQ(frame.height, first->desc().buffer_height);
  }
  // The AR hit test normalises against the same committed size, so it is pinned
  // too rather than running ahead of the panel the operator is pointing at.
  EXPECT_EQ(stream.logical_width(), 200u);
}

TEST(PanelStream, ZeroingTheHysteresisRestoresRebuildOnAnyChange)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = options_no_pacing();
  options.resize_threshold_px = 0;
  options.resize_settle_seconds = 0.0;
  options.resize_settle_ticks = 0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(202, 120);
  fixture.widget()->layout()->activate();
  ASSERT_TRUE(stream.tick(1.0));

  EXPECT_EQ(factory->create_calls(), 2u) << "a host that opts out gets a rebuild on every change";
  EXPECT_EQ(factory->latest()->desc().buffer_width, 202u);
}

TEST(PanelStream, TheNotReadyStreakCountsConsecutiveUnreservedTicksAndResetsOnASubmit)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  // The keepalive is what keeps an idle panel ASKING for a send cycle, which is
  // exactly the situation the streak has to describe.
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 1.0;
  QtPanelStream stream("panel", fixture.widget(), options, factory);
  ASSERT_TRUE(stream.tick(0.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);

  // "Nothing has attached to this panel's stream yet" is a STATE, not an error,
  // and it is the one the operator has to be able to see: the adapter does not
  // print it every frame, so this streak is what says it is still happening.
  factory->latest()->set_ready(false);
  for (int i = 1; i <= 3; ++i) {
    EXPECT_FALSE(stream.tick(static_cast<double>(i)));
  }
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 3u);
  EXPECT_EQ(stream.diagnostics().frames_skipped_not_ready, 3u);
  EXPECT_TRUE(stream.diagnostics().last_error.empty()) << "not-ready is not a failure";

  factory->latest()->set_ready(true);
  EXPECT_TRUE(stream.tick(10.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);
}

TEST(PanelStream, ANotYetLaidOutWidgetIsRefusedWithoutTouchingTheFactory)
{
  QWidget widget;
  widget.resize(0, 0);

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", &widget, options_no_pacing(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_EQ(factory->create_calls(), 0u);
  EXPECT_FALSE(stream.diagnostics().last_error.empty());
}

TEST(PanelStream, AFactoryRefusalIsReportedAndRetriedNextTick)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_fails(true, "session not ready");
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_EQ(stream.diagnostics().last_error, "session not ready");
  EXPECT_FALSE(stream.diagnostics().active);

  factory->set_fails(false);
  EXPECT_TRUE(stream.tick(1.0));
  EXPECT_TRUE(stream.diagnostics().active);
}

TEST(PanelStream, ClosingClosesTheSenderAndIsIdempotent)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  stream.close();
  stream.close();
  EXPECT_EQ(factory->latest()->close_calls(), 1u);
  EXPECT_FALSE(stream.diagnostics().active);
}

TEST(PanelStream, ClosingReturnsWhatTheAdapterDidAndNothingTheSecondTime)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  const qar_ar_core::PanelCloseReport first = stream.close();
  EXPECT_TRUE(first.input_unsubscribed);
  EXPECT_EQ(first.panel, qar_ar_core::PanelCloseOutcome::Closed);
  EXPECT_TRUE(first.sender_destroyed);
  EXPECT_FALSE(factory->latest()->last_close_panel_closed_in_ar());

  const qar_ar_core::PanelCloseReport second = stream.close();
  EXPECT_FALSE(second.input_unsubscribed);
  EXPECT_EQ(second.panel, qar_ar_core::PanelCloseOutcome::NoPanel);
  EXPECT_FALSE(second.sender_destroyed);
  EXPECT_EQ(factory->latest()->sender_destroy_calls(), 1u);
}

TEST(PanelStream, ClosingAStreamTheOperatorClosedInArTellsTheAdapterToSkipClosePanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  stream.suspend_capture();  // what the manager does when the runtime reports CLOSED
  const qar_ar_core::PanelCloseReport report = stream.close();

  EXPECT_TRUE(factory->latest()->last_close_panel_closed_in_ar());
  EXPECT_EQ(report.panel, qar_ar_core::PanelCloseOutcome::AlreadyClosedInAr);
  EXPECT_EQ(factory->latest()->close_panel_calls(), 0u);
  EXPECT_TRUE(report.sender_destroyed);
}

TEST(PanelStream, AResizeRebuildStillClosesThePanelBecauseNothingClosedItInAr)
{
  // The one mid-session close that is not a window closing: the resize rebuild
  // closes the old panel and sender before asking for new ones. It never passes the
  // AR-closed flag (a panel closed in AR is not captured, so never rebuilt).
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  fixture.widget()->resize(320, 200);
  fixture.widget()->layout()->activate();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));  // settled: the sender is rebuilt

  ASSERT_EQ(factory->created().size(), 2u);
  const auto old_adapter = factory->created().front();
  EXPECT_EQ(old_adapter->close_calls(), 1u);
  EXPECT_EQ(old_adapter->close_panel_calls(), 1u);
  EXPECT_FALSE(old_adapter->last_close_panel_closed_in_ar());
  EXPECT_EQ(factory->created().back()->close_calls(), 0u);
}

TEST(PanelStream, SetPoseIsForwardedToTheLiveSender)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{1.0f, 2.0f, 3.0f};
  stream.set_pose(pose);

  EXPECT_EQ(factory->latest()->placement_calls(), 1u);
  EXPECT_FLOAT_EQ(factory->latest()->placement().pose.position.y, 2.0f);
  EXPECT_FLOAT_EQ(stream.placement().pose.position.z, 3.0f);
}

TEST(PanelStream, SubmittedPixelsMatchTheCapturedBuffer)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));

  ASSERT_EQ(factory->latest()->frames().size(), 1u);
  const auto & frame = factory->latest()->frames().front();
  EXPECT_EQ(frame.width, 200u);
  EXPECT_EQ(frame.height, 120u);
  EXPECT_EQ(frame.rgba.size(), 200u * 120u * 4u);
}

// --- manager ---------------------------------------------------------------

namespace
{

QtPanelStreamingConfig
make_config(std::shared_ptr<MockQarPanelSenderFactory> factory, std::shared_ptr<ManualClock> clock)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  config.clock = std::move(clock);
  return config;
}

}  // namespace

TEST(PanelManager, RefusesConstructionWithoutAFactory)
{
  QtPanelStreamingConfig config;
  EXPECT_THROW(QtPanelStreamManager{config}, std::invalid_argument);
}

TEST(PanelManager, AddsRemovesAndListsPanelsDeterministically)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("zeta", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("alpha", b.widget(), options_no_pacing()).ok());

  const auto keys = manager.panel_keys();
  ASSERT_EQ(keys.size(), 2u);
  EXPECT_EQ(keys[0], "alpha") << "keys must come back ordered";
  EXPECT_EQ(keys[1], "zeta");
  EXPECT_TRUE(manager.has_panel("zeta"));

  manager.tick();

  // remove_panel follows the close mode, and the default is DESTROY: the panel's
  // AR panel is closed, its sender destroyed, and the key is simply gone.
  ASSERT_TRUE(manager.remove_panel("zeta").ok());
  EXPECT_FALSE(manager.has_panel("zeta"));
  EXPECT_FALSE(manager.is_tombstoned("zeta"));
  EXPECT_EQ(manager.panel_keys(), std::vector<std::string>{"alpha"});
  EXPECT_EQ(manager.remove_panel("zeta").status, Status::NotFound) << "a panel is closed once";
  EXPECT_EQ(manager.remove_panel("nothing-here").status, Status::NotFound);
}

TEST(PanelManager, RemovingAPanelInNoticeModeTombstonesIt)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;  // the escape hatch
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("zeta", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("alpha", b.widget(), options_no_pacing()).ok());

  // A sender has to exist before there is anything to protect: a panel removed
  // before its first tick never announced a stream, so it is simply erased (see
  // PanelLifetime.APanelThatNeverGotASenderIsErasedRatherThanTombstoned).
  manager.tick();

  // In Notice mode remove_panel TOMBSTONES: the key stays, showing the
  // closed-source notice, because its video sender must outlive the widget.
  ASSERT_TRUE(manager.remove_panel("zeta").ok());
  EXPECT_TRUE(manager.has_panel("zeta"));
  EXPECT_TRUE(manager.is_tombstoned("zeta"));
  EXPECT_TRUE(manager.remove_panel("zeta").ok()) << "tombstoning twice is a no-op";
  EXPECT_EQ(manager.remove_panel("nothing-here").status, Status::NotFound);
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 0u) << "no sender is destroyed mid-session in notice mode";
  }
}

TEST(PanelManager, RejectsDuplicateKeysAndBadArguments)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  EXPECT_EQ(
    manager.add_panel("p", fixture.widget(), options_no_pacing()).status, Status::AlreadyExists);
  EXPECT_EQ(manager.add_panel("", fixture.widget(), options_no_pacing()).status,
    Status::InvalidArgument);
  EXPECT_EQ(manager.add_panel("q", nullptr, options_no_pacing()).status, Status::InvalidArgument);
}

TEST(PanelManager, AddingAPanelDoesNotTouchQarOsUntilTheFirstTick)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  EXPECT_EQ(factory->create_calls(), 0u) << "add_panel must not block on the runtime";

  EXPECT_EQ(manager.tick(), 1u);
  EXPECT_EQ(factory->create_calls(), 1u);
}

TEST(PanelManager, TickReportsHowManyPanelsActuallySubmitted)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamManager manager(make_config(factory, clock));

  ASSERT_TRUE(manager.add_panel("a", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("b", b.widget(), options_no_pacing()).ok());

  EXPECT_EQ(manager.tick(), 2u);
  clock->advance(1.0);
  EXPECT_EQ(manager.tick(), 0u) << "nothing changed, so nothing should be sent";
}

TEST(PanelManager, DiagnosticsSummariseEveryPanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  QtPanelStreamManager manager(make_config(factory, clock));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();
  clock->advance(1.0);
  manager.tick();

  const auto snapshot = manager.diagnostics();
  EXPECT_EQ(snapshot.panel_count, 1u);
  EXPECT_EQ(snapshot.ticks, 2u);
  EXPECT_TRUE(snapshot.streaming_active);
  ASSERT_EQ(snapshot.panels.size(), 1u);
  EXPECT_EQ(snapshot.panels[0].key, "p");
  EXPECT_EQ(snapshot.panels[0].frames_submitted, 1u);
  EXPECT_EQ(snapshot.panels[0].frames_skipped_unchanged, 1u);
}

TEST(PanelManager, ClearClosesEveryPanel)
{
  Fixture a;
  Fixture b;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("a", a.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(manager.add_panel("b", b.widget(), options_no_pacing()).ok());
  manager.tick();

  manager.clear();
  EXPECT_TRUE(manager.panel_keys().empty());
  for (const auto & adapter : factory->created()) {
    EXPECT_EQ(adapter->close_calls(), 1u);
  }
}

TEST(PanelManager, SetPanelPoseTargetsTheNamedPanel)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{0.0f, 1.5f, -2.0f};
  EXPECT_TRUE(manager.set_panel_pose("p", pose).ok());
  EXPECT_EQ(manager.set_panel_pose("missing", pose).status, Status::NotFound);
  EXPECT_FLOAT_EQ(factory->latest()->placement().pose.position.y, 1.5f);
}

TEST(PanelManager, RoutesARoomPointToThePanelPixelUnderIt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  auto clock = std::make_shared<ManualClock>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  // A tick is what derives the panel's physical size from the widget, so it must
  // happen before a hit test can mean anything.
  manager.tick();

  // The panel's pose sits on its TOP EDGE (QarOS panel space), so the point
  // that lands on the widget's centre is half a panel height below it.
  float height_m = 0.0f;
  for (const auto & p : manager.diagnostics().panels) {
    if (p.key == "p") { height_m = p.height_meters; }
  }
  ASSERT_GT(height_m, 0.0f);
  EXPECT_TRUE(manager.route_room_point(
      QarVector3{0.0f, -0.5f * height_m, 0.0f}, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(sink.events().size(), 1u);
  EXPECT_EQ(sink.events()[0].panel_key, "p");
  EXPECT_EQ(sink.events()[0].kind, PanelPointerEvent::Kind::Click);
  EXPECT_EQ(sink.events()[0].widget_x, 100);
  EXPECT_EQ(sink.events()[0].widget_y, 60);
}

TEST(PanelManager, ARoomPointThatMissesEveryPanelNotifiesNobody)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  EXPECT_FALSE(manager.route_room_point(QarVector3{9.0f, 0.0f, 0.0f}, PanelPointerEvent::Kind::Hover));
  EXPECT_TRUE(sink.events().empty());
}

TEST(PanelManager, RoutesARoomRayThroughTheSameSeam)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingPanelInputSink sink;

  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.input_sink = &sink;
  QtPanelStreamManager manager(config);

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();

  EXPECT_TRUE(manager.route_room_ray(
      QarVector3{0.0f, 0.0f, 1.0f}, QarVector3{0.0f, 0.0f, -1.0f}, PanelPointerEvent::Kind::Click));
  ASSERT_EQ(sink.events().size(), 1u);
  EXPECT_EQ(sink.events()[0].widget_x, 100);
}

TEST(PanelManager, RoutingWithoutASinkIsSafe)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager(make_config(factory, std::make_shared<ManualClock>()));

  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_no_pacing()).ok());
  manager.tick();
  EXPECT_TRUE(manager.route_room_point(QarVector3{0.0f, 0.0f, 0.0f}, PanelPointerEvent::Kind::Click));
}


// ---------------------------------------------------------------------------
// A panel QarOS is asking for and we are not serving must be VISIBLE.
//
// The shape being modelled: a GUI panel is created, QarOS is told its content
// is stream <id> and starts asking for it, and our sender never reports a ready
// destination — so we submit nothing, forever, and without a report nothing
// would say so. The test double's `set_senders_start_not_ready` is exactly
// `try_begin_frame` returning success with out_ready == false, which is how the
// runtime reports MEDIA_STREAMING_NO_ACTIVE_TRANSFERS (604).
// ---------------------------------------------------------------------------
namespace
{

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    qar_ar_core::DiagnosticSeverity severity;
    std::string category;
    std::string message;
    std::string panel_key;
  };

  void on_message(
    qar_ar_core::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message,
    const std::string & panel_key) override
  {
    messages.push_back(Entry{severity, category, message, panel_key});
  }

  /// The first entry whose message contains @p needle, or nullptr.
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

/// Pacing off, keepalive ON (the shipped default): an unserved panel must keep
/// ATTEMPTING a submit every tick, otherwise "not ready" would really mean
/// "we never asked".
PanelStreamOptions
options_unpaced_with_keepalive()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 1.0;
  return options;
}

}  // namespace

TEST(PanelStream, TheStreamIdIsRecordedWhenTheSenderIsBuilt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", fixture.widget(), options_no_pacing(), factory);

  ASSERT_TRUE(stream.tick(0.0));
  // Whatever the backend calls its stream, the panel carries it — that pairing
  // is the whole point: the runtime names streams and nothing else.
  EXPECT_EQ(stream.diagnostics().stream_id, factory->latest()->stream_id_text());
  EXPECT_FALSE(stream.diagnostics().stream_id.empty());
}

TEST(PanelStream, AnUnservedPanelAccumulatesUnservedSecondsAndNoFrames)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  QtPanelStream stream("panel", fixture.widget(), options_unpaced_with_keepalive(), factory);

  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_FALSE(stream.tick(3.0));
  EXPECT_FALSE(stream.tick(9.0));

  const auto & d = stream.diagnostics();
  EXPECT_EQ(d.frames_submitted, 0u);
  // Every tick asked and was refused — so the silence is QarOS's, not ours.
  EXPECT_EQ(d.not_ready_streak, 3u);
  EXPECT_EQ(factory->latest()->begin_calls(), 3u);
  EXPECT_NEAR(d.unserved_seconds, 9.0, 1e-9);
}

TEST(PanelStream, TheUnservedClockResetsOnTheFirstFrameThatLands)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  QtPanelStream stream("panel", fixture.widget(), options_unpaced_with_keepalive(), factory);

  ASSERT_FALSE(stream.tick(0.0));
  ASSERT_FALSE(stream.tick(4.0));
  ASSERT_GT(stream.diagnostics().unserved_seconds, 0.0);

  factory->latest()->set_ready(true);
  ASSERT_TRUE(stream.tick(5.0));
  EXPECT_DOUBLE_EQ(stream.diagnostics().unserved_seconds, 0.0);
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 0u);
}

TEST(PanelManager, WarnsOnceWhenAPanelHasHeldASenderWithoutServingIt)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.unserved_warn_seconds = 5.0;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());

  manager.tick();
  clock->advance(4.0);
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 0u) << "warned before the threshold";

  clock->advance(2.0);  // 6 s of holding a sender and submitting nothing
  manager.tick();
  const auto * warning = diagnostics.find("is not being served");
  ASSERT_NE(warning, nullptr);
  EXPECT_EQ(warning->severity, qar_ar_core::DiagnosticSeverity::Warning);
  EXPECT_EQ(warning->panel_key, "p");
  // The stream the headset is asking for has to be IN the line, or it cannot be
  // matched to the runtime's streamRequest/<id> traffic.
  EXPECT_NE(warning->message.find(factory->latest()->stream_id_text()), std::string::npos);

  // Latched: once per unserved run, not once per tick.
  clock->advance(10.0);
  manager.tick();
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 1u);
}

TEST(PanelManager, RetiringAPanelThatNeverServedAFrameIsAWarningNamingTheStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();

  clock->advance(3.0);
  manager.tick();
  ASSERT_TRUE(manager.remove_panel("p").ok());

  const auto * retired = diagnostics.find("having served it NO frames");
  ASSERT_NE(retired, nullptr);
  EXPECT_EQ(retired->severity, qar_ar_core::DiagnosticSeverity::Warning);
  EXPECT_NE(retired->message.find(stream_id), std::string::npos);
}

TEST(PanelManager, RetiringAPanelThatDidServeReportsTheFrameCountAndStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();
  ASSERT_GT(factory->latest()->submit_calls(), 0u);

  ASSERT_TRUE(manager.remove_panel("p").ok());

  // The close line names the stream and the number of frames that stream
  // actually carried — and it is NOT the warning, which is for a stream that
  // served nothing.
  const auto * closed = diagnostics.find("closed 'p'");
  ASSERT_NE(closed, nullptr);
  EXPECT_EQ(closed->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_NE(closed->message.find(stream_id), std::string::npos);
  EXPECT_NE(closed->message.find("after 1 frame(s)"), std::string::npos);
  EXPECT_EQ(diagnostics.count("having served it NO frames"), 0u);
}

TEST(PanelManager, TombstoningAPanelInNoticeModeReportsTheFrameCountAndStream)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.close_mode = qar_qt_streaming::PanelCloseMode::Notice;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());
  manager.tick();
  const std::string stream_id = factory->latest()->stream_id_text();
  ASSERT_GT(factory->latest()->submit_calls(), 0u);

  ASSERT_TRUE(manager.remove_panel("p").ok());

  // The retirement line is a TOMBSTONE line — the stream is not removed, it
  // goes on showing the notice — but it still names the stream and the number
  // of frames that stream actually carried.
  const auto * retired = diagnostics.find("has closed after");
  ASSERT_NE(retired, nullptr);
  EXPECT_EQ(retired->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_NE(retired->message.find(stream_id), std::string::npos);
  EXPECT_NE(retired->message.find("1 frame(s)"), std::string::npos);
  EXPECT_EQ(diagnostics.count("having served it NO frames"), 0u);
}

TEST(PanelManager, ZeroUnservedWarnSecondsDisablesTheReport)
{
  Fixture fixture;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  RecordingDiagnostics diagnostics;
  auto clock = std::make_shared<ManualClock>();

  QtPanelStreamingConfig config = make_config(factory, clock);
  config.diagnostics = &diagnostics;
  config.unserved_warn_seconds = 0.0;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", fixture.widget(), options_unpaced_with_keepalive()).ok());

  manager.tick();
  clock->advance(60.0);
  manager.tick();
  EXPECT_EQ(diagnostics.count("is not being served"), 0u);
}

// ---------------------------------------------------------------------------
// A popup opening or closing over a panel is logged once per edge.
//
// The line is how an operator tells "no list opened" from "a list opened and was kept off the
// desktop", so it must appear on the 0 -> N and N -> 0 edges only - never once per captured
// frame while the list stays open.
// ---------------------------------------------------------------------------

TEST(QtPanelStreamManager, APopupOpeningAndClosingIsLoggedOnceEach)
{
  // Declared before the manager, so the manager (and its streams) is gone before the widget.
  std::unique_ptr<QWidget> widget(new QWidget());
  auto * combo = new QComboBox(widget.get());
  combo->addItems({"zero", "one", "two"});
  combo->setGeometry(20, 20, 160, 28);
  ASSERT_TRUE(qar_qt_streaming::show_offscreen(widget.get(), 300, 200));
  widget->ensurePolished();
  QCoreApplication::processEvents();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  RecordingDiagnostics diagnostics;
  QtPanelStreamingConfig config = make_config(factory, std::make_shared<ManualClock>());
  config.diagnostics = &diagnostics;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("off", widget.get(), options_no_pacing()).ok());
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 0u) << "nothing is open yet";
  EXPECT_EQ(diagnostics.count("popup closed"), 0u);

  // An AR tap in the middle of the combo opens its list.
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = "off";
  sample.u = 100.0f / 300.0f;
  sample.v = 34.0f / 200.0f;
  ASSERT_TRUE(manager.handle_sample(sample));
  QCoreApplication::processEvents();
  ASSERT_NE(QApplication::activePopupWidget(), nullptr) << "the AR tap did not open the list";

  manager.tick();
  ASSERT_EQ(diagnostics.count("popup open over this panel"), 1u);
  const auto * opened = diagnostics.find("popup open over this panel");
  ASSERT_NE(opened, nullptr);
  EXPECT_EQ(opened->severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_EQ(opened->panel_key, "off");
  EXPECT_NE(opened->message.find("kept off the desktop"), std::string::npos) << opened->message;

  // Open and unchanged: the count does not move, so nothing more is said.
  manager.tick();
  manager.tick();
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 1u);
  EXPECT_EQ(diagnostics.count("popup closed"), 0u);

  combo->hidePopup();
  QCoreApplication::processEvents();
  manager.tick();
  ASSERT_EQ(diagnostics.count("popup closed"), 1u);
  EXPECT_EQ(diagnostics.find("popup closed")->panel_key, "off");
  manager.tick();
  EXPECT_EQ(diagnostics.count("popup closed"), 1u);
  EXPECT_EQ(diagnostics.count("popup open over this panel"), 1u);
}
