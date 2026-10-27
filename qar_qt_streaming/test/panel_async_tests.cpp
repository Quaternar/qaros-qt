// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// A sender that only queues its QarOS calls: it is still being made for a while (pending, not
// ready with the cause Creating), it accepts a hide or a placement and refuses it later, and its
// creation can fail. The stream takes those outcomes from drain_results() and acts on them. The
// recording adapter scripts the outcomes; what the real adapter does on the session's thread
// needs the SDK and belongs to the tests of qar_qar_adapter.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QCoreApplication>
#include <QPalette>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using qar_ar_core::PanelNotReadyCause;
using qar_ar_core::PanelOpResult;
using qar_ar_core::PanelStreamDiagnostics;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderAdapter;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

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

PanelOpResult
create_result(bool ok, std::string error = std::string())
{
  PanelOpResult result;
  result.op = PanelOpResult::Op::Create;
  result.ok = ok;
  result.error = std::move(error);
  return result;
}

PanelOpResult
state_result(QarGuiPanelState state, bool ok, std::string error = std::string())
{
  PanelOpResult result;
  result.op = PanelOpResult::Op::State;
  result.state = state;
  result.ok = ok;
  result.error = std::move(error);
  return result;
}

PanelOpResult
placement_result(bool ok, std::string error)
{
  PanelOpResult result;
  result.op = PanelOpResult::Op::Placement;
  result.ok = ok;
  result.error = std::move(error);
  return result;
}

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  void on_message(
    qar_ar_core::DiagnosticSeverity severity, const std::string & category,
    const std::string & message, const std::string & panel_key) override
  {
    (void)severity;
    (void)category;
    (void)panel_key;
    lines.push_back(message);
  }

  std::size_t count(const std::string & needle) const
  {
    std::size_t n = 0;
    for (const std::string & line : lines) {
      n += line.find(needle) != std::string::npos ? 1u : 0u;
    }
    return n;
  }

  std::vector<std::string> lines;
};

bool
contains(const std::vector<std::string> & lines, const std::string & needle)
{
  for (const std::string & line : lines) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

// --- a sender that is still being made ---------------------------------------------

TEST(PanelAsync, CreatingIsNotUnservedAndTheStreamIdIsReadWhenTheCreationSucceeds)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_pending(true);
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);

  // Thirty seconds of ticks while the panel is made on the session's thread.
  for (int i = 0; i < 60; ++i) {
    EXPECT_FALSE(stream.tick(0.5 * i));
  }
  const PanelStreamDiagnostics & waiting = stream.diagnostics();
  EXPECT_TRUE(waiting.active);
  EXPECT_EQ(waiting.frames_submitted, 0u);
  EXPECT_EQ(waiting.not_ready_streak, 0u) << "waiting for the creation is not a not-ready streak";
  EXPECT_EQ(waiting.frames_skipped_not_ready, 0u);
  EXPECT_EQ(waiting.unserved_seconds, 0.0) << "the clock of the unserved panel is not running";
  EXPECT_TRUE(waiting.stream_id.empty()) << "the id exists once the creation is done";
  ASSERT_EQ(factory->create_calls(), 1u);
  const auto adapter = factory->latest();
  EXPECT_EQ(adapter->begin_calls(), 0u) << "no frame is asked for while the panel is pending";
  EXPECT_GT(adapter->drain_calls(), 0u) << "the outcomes are asked for on every tick";

  adapter->set_pending(false);
  adapter->set_stream_id("late-id");
  adapter->queue_result(create_result(true));
  EXPECT_TRUE(stream.tick(31.0)) << "the first frame goes out on the tick that sees the panel";
  EXPECT_EQ(stream.diagnostics().stream_id, "late-id");
  EXPECT_EQ(stream.diagnostics().frames_submitted, 1u);
  EXPECT_EQ(stream.diagnostics().unserved_seconds, 0.0);
}

TEST(PanelAsync, ACreatingCauseFromTheSenderCountsNothingEither)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_not_ready(true);
  PanelStreamOptions options = options_no_pacing();
  options.keepalive_seconds = 0.2;  // a panel that has sent nothing asks for a frame on every tick
  QtPanelStream stream("p", source.get(), options, factory);
  EXPECT_FALSE(stream.tick(0.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 1u) << "the default cause is counted";
  const auto adapter = factory->latest();

  // A sender that is not pending but says "not ready, creating" (the cause is the contract).
  adapter->set_not_ready_cause(PanelNotReadyCause::Creating);
  for (int i = 1; i <= 20; ++i) {
    EXPECT_FALSE(stream.tick(1.0 * i));
  }
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 1u);
  EXPECT_EQ(stream.diagnostics().frames_skipped_not_ready, 1u);
  EXPECT_EQ(stream.diagnostics().unserved_seconds, 0.0);

  // The same sender with a real cause is counted as before.
  adapter->set_not_ready_cause(PanelNotReadyCause::NoDestination);
  EXPECT_FALSE(stream.tick(30.0));
  EXPECT_EQ(stream.diagnostics().not_ready_streak, 2u);
}

TEST(PanelAsync, TheManagerSaysNothingAboutAnUnservedPanelWhileItIsCreated)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_pending(true);
  auto clock = std::make_shared<ManualClock>();
  RecordingDiagnostics diagnostics;
  QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;
  config.clock = clock;
  config.diagnostics = &diagnostics;
  config.pump_input_in_tick = false;
  QtPanelStreamManager manager(config);
  ASSERT_TRUE(manager.add_panel("p", source.get(), options_no_pacing()).ok());
  for (int i = 0; i < 40; ++i) {
    clock->advance(1.0);
    manager.tick();
  }
  EXPECT_EQ(diagnostics.count("submitted no frame"), 0u) << "no unserved warning";
  EXPECT_EQ(diagnostics.count("UNSERVED"), 0u);
  EXPECT_EQ(manager.diagnostics().panels.at(0).unserved_seconds, 0.0);
}

// --- a creation that fails ---------------------------------------------------------

TEST(PanelAsync, AFailedCreationResultMarksThePanelInactiveAndAsksAgainAfterAPause)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_start_pending(true);
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  EXPECT_FALSE(stream.tick(0.0));
  ASSERT_EQ(factory->create_calls(), 1u);
  EXPECT_TRUE(stream.diagnostics().active);

  factory->latest()->queue_result(create_result(false, "the Hub has no room for the panel"));
  EXPECT_FALSE(stream.tick(0.1));
  EXPECT_FALSE(stream.diagnostics().active);
  EXPECT_EQ(stream.diagnostics().last_error, "the Hub has no room for the panel");
  EXPECT_FALSE(stream.has_adapter()) << "the failed sender is dropped";

  // Not on every tick: the next try waits for the pause.
  EXPECT_FALSE(stream.tick(0.5));
  EXPECT_EQ(factory->create_calls(), 1u);

  factory->set_senders_start_pending(false);
  EXPECT_TRUE(stream.tick(0.1 + QtPanelStream::kCreateRetrySeconds));
  EXPECT_EQ(factory->create_calls(), 2u);
  EXPECT_TRUE(stream.diagnostics().active);
  EXPECT_EQ(stream.diagnostics().frames_submitted, 1u);
}

// --- a placement that is refused after it was accepted -----------------------------

TEST(PanelAsync, ARefusedPlacementResultIsRecordedAsTheStreamsError)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const auto adapter = factory->latest();
  adapter->set_ready(false);  // nothing is submitted, so nothing clears the error
  stream.set_pose(QarPose{{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f}});
  EXPECT_EQ(adapter->placement_calls(), 1u) << "accepted at once";
  adapter->queue_result(placement_result(false, "the panel is gone"));
  EXPECT_FALSE(stream.tick(1.0));
  EXPECT_EQ(stream.diagnostics().last_error, "the panel is gone");
}

// --- a hide or show that is refused after it was accepted --------------------------

TEST(PanelAsync, AHideThatWasAcceptedAndRefusedLaterIsUndone)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const auto adapter = factory->latest();

  stream.request_ar_visible(false);
  EXPECT_FALSE(stream.tick(1.0)) << "hidden: nothing is captured";
  EXPECT_TRUE(stream.diagnostics().ar_hidden) << "assumed on accept";
  const std::uint64_t changes = stream.diagnostics().visibility_changes;
  EXPECT_EQ(changes, 1u);

  adapter->queue_result(
    state_result(QAR_GUI_PANEL_STATE_HIDDEN, false, "QarOS would not hide the panel"));
  stream.tick(2.0);
  EXPECT_FALSE(stream.diagnostics().ar_hidden) << "the panel is visible after all";
  EXPECT_EQ(stream.diagnostics().visibility_failures, 1u);
  EXPECT_EQ(stream.diagnostics().last_visibility_error, "QarOS would not hide the panel");
  EXPECT_EQ(stream.diagnostics().visibility_changes, 0u);
}

TEST(PanelAsync, AShowThatWasRefusedLaterLeavesThePanelHidden)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const auto adapter = factory->latest();
  stream.request_ar_visible(false);
  EXPECT_FALSE(stream.tick(1.0));
  stream.request_ar_visible(true);
  EXPECT_TRUE(stream.tick(2.0));
  EXPECT_FALSE(stream.diagnostics().ar_hidden);

  // The hide result is the first in the queue (accepted), the show result the second.
  adapter->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, true));
  adapter->queue_result(state_result(QAR_GUI_PANEL_STATE_VISIBLE, false, "no show"));
  EXPECT_FALSE(stream.tick(3.0)) << "the panel is hidden again, so nothing is captured";
  EXPECT_TRUE(stream.diagnostics().ar_hidden);
  EXPECT_EQ(stream.diagnostics().visibility_failures, 1u);
}

TEST(PanelAsync, AKeptPanelWhoseHideWasRefusedLaterIsAskedAgainAndTheEndIsSaid)
{
  SourceWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  stream.set_replaced_panel_policy([]() { return true; });
  ASSERT_TRUE(stream.tick(0.0));
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));
  ASSERT_EQ(factory->create_calls(), 2u);
  qar_qt_streaming::SizeRebuildReport report;
  ASSERT_TRUE(stream.take_size_rebuild(report));
  EXPECT_EQ(report.fate, qar_qt_streaming::ReplacedPanelFate::HiddenKept)
    << "the hide was accepted";
  const auto old_panel = factory->created()[0];
  EXPECT_EQ(old_panel->state_calls(), 1u);

  // The first refusal arrives: the hide is asked for again after the pause.
  old_panel->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, false, "hub busy"));
  stream.tick(1.7);
  EXPECT_TRUE(contains(stream.take_notes(), "could not be hidden (hub busy); it is asked again"));
  stream.tick(2.0);
  EXPECT_EQ(old_panel->state_calls(), 1u) << "not before the pause is over";
  stream.tick(1.7 + QtPanelStream::kKeptHideRetrySeconds + 0.01);
  EXPECT_EQ(old_panel->state_calls(), 2u);

  // Every further request is accepted and refused again; after the last attempt the end is said.
  double now = 3.0;
  for (std::uint32_t attempt = 2; attempt <= QtPanelStream::kKeptHideRetries + 1; ++attempt) {
    old_panel->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, false, "hub busy"));
    stream.tick(now);
    now += QtPanelStream::kKeptHideRetrySeconds + 0.01;
    stream.tick(now);
  }
  EXPECT_EQ(old_panel->state_calls(), QtPanelStream::kKeptHideRetries + 1)
    << "the first request and the retries, no more";
  EXPECT_TRUE(contains(stream.take_warnings(), "still visible after 4 attempts"));
  stream.tick(now + 10.0);
  EXPECT_EQ(old_panel->state_calls(), QtPanelStream::kKeptHideRetries + 1);
}

TEST(PanelAsync, AHideThatKeepsBeingAcceptedIsNotAskedAgain)
{
  SourceWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  stream.set_replaced_panel_policy([]() { return true; });
  ASSERT_TRUE(stream.tick(0.0));
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));
  const auto old_panel = factory->created()[0];
  old_panel->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, true));
  for (int i = 0; i < 10; ++i) {
    stream.tick(2.0 + i);
  }
  EXPECT_EQ(old_panel->state_calls(), 1u);
  EXPECT_TRUE(stream.take_warnings().empty());
}

// --- the end of the session with an outcome that was not taken yet ------------------

TEST(PanelAsync, TheEndOfASessionTakesTheRefusalOfAnAcceptedHideBeforeItDecides)
{
  SourceWidget source;
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  factory->set_senders_support_detach(true);
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const auto adapter = factory->latest();
  stream.request_ar_visible(false);
  EXPECT_FALSE(stream.tick(1.0));
  ASSERT_TRUE(stream.diagnostics().ar_hidden) << "assumed when the hide was accepted";
  ASSERT_EQ(adapter->state_calls(), 1u);

  // The refusal is queued and no tick has taken it: the stream must not think the panel hidden.
  adapter->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, false, "hub busy"));
  const qar_qt_streaming::SessionEndReport report = stream.end_session();
  EXPECT_EQ(adapter->state_calls(), 2u) << "the panel is hidden again before it is left";
  EXPECT_EQ(report.panels, 1u);
  EXPECT_EQ(report.left_in_place, 1u);
}

TEST(PanelAsync, TheEndOfASessionHidesAKeptPanelWhoseHideWasRefusedAndNotTakenYet)
{
  SourceWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  stream.set_replaced_panel_policy([]() { return true; });
  ASSERT_TRUE(stream.tick(0.0));
  source.get()->resize(400, 240);
  QCoreApplication::processEvents();
  stream.tick(1.0);
  stream.tick(1.3);
  ASSERT_TRUE(stream.tick(1.6));
  const auto old_panel = factory->created()[0];
  ASSERT_EQ(old_panel->state_calls(), 1u);
  old_panel->set_supports_detach(true);  // it is left in place at the end, not closed

  old_panel->queue_result(state_result(QAR_GUI_PANEL_STATE_HIDDEN, false, "hub busy"));
  const qar_qt_streaming::SessionEndReport report = stream.end_session();
  EXPECT_EQ(old_panel->state_calls(), 2u) << "the kept panel is hidden again before it is left";
  EXPECT_EQ(report.panels, 2u);
}
