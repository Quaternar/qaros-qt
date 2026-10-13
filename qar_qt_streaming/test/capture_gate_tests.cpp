// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The capture gate (capture_gate.hpp): the pure decision, and QtPanelStream
// driven through it with a manual clock.
//
// The scenario behind all of it: a 30 fps video panel whose stream is ticked
// only on Windows' default 15.625 ms timer granularity (what RViz gets once a
// full-screen window covers it). A strict limiter then needs three ticks
// (46.9 ms) between captures, so 30 fps becomes 21.3 fps.
//
// Include order note: qar_qt_streaming project headers first, plain Qt headers
// after -- see the `emit` note in qt_panel_stream.hpp.

#include <gtest/gtest.h>

#include "qar_qt_streaming/capture_gate.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QPalette>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>

using qar_qt_streaming::CaptureGateInput;
using qar_qt_streaming::CaptureGateVerdict;
using qar_qt_streaming::LatestVideoFrame;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::TickIntervalEstimator;
using qar_qt_streaming::VideoPanelWidget;
using qar_qt_streaming::capture_gate_idle_seconds;
using qar_qt_streaming::capture_gate_slack_seconds;
using qar_qt_streaming::decide_capture;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

constexpr double kWindowsTick = 0.015625;  // Windows' default timer granularity
constexpr double kThirtyFps = 1.0 / 30.0;

/// A gate input for a panel that captured at t = 0 and is asked again at @p now.
CaptureGateInput
after_a_capture(double now, std::uint32_t max_fps)
{
  CaptureGateInput in;
  in.now_seconds = now;
  in.has_captured = true;
  in.last_capture_seconds = 0.0;
  in.max_fps = max_fps;
  return in;
}

QImage
gray_frame(int width, int height, int index)
{
  // Consecutive frames always differ, so every capture is "changed".
  const int level = 20 + (index * 37) % 200;
  QImage img(width, height, QImage::Format_RGBA8888);
  img.fill(QColor(level, level, level));
  return img;
}

PanelStreamOptions
video_options(bool capture_on_arrival)
{
  PanelStreamOptions options;
  options.max_fps = 30;
  options.keepalive_seconds = 0.0;
  options.input.enabled = false;
  options.capture_on_frame_arrival = capture_on_arrival;
  return options;
}

struct SimResult
{
  std::uint64_t published = 0;
  std::uint64_t captured = 0;
  std::uint64_t submitted = 0;
  bool frame_driven = false;
};

/// Publish a @p fps video into @p source on its own clock while @p stream is
/// ticked every @p tick_dt seconds, for @p seconds. Frames land BEFORE the tick
/// at the same instant, like a decode worker finishing between two ticks.
SimResult
run_sim(
  QtPanelStream & stream, LatestVideoFrame & source, int frame_w, int frame_h, double fps,
  double tick_dt, double seconds)
{
  SimResult result;
  const double frame_period = 1.0 / fps;
  double next_frame_at = 0.0;
  const int ticks = static_cast<int>(seconds / tick_dt);
  for (int n = 0; n < ticks; ++n) {
    const double now = n * tick_dt;
    while (next_frame_at <= now + 1e-9) {
      source.publish(gray_frame(frame_w, frame_h, static_cast<int>(result.published)));
      ++result.published;
      next_frame_at += frame_period;
    }
    stream.tick(now);
  }
  result.captured = stream.diagnostics().frames_captured;
  result.submitted = stream.diagnostics().frames_submitted;
  result.frame_driven = stream.diagnostics().frame_driven;
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// The pure decision
// ---------------------------------------------------------------------------

TEST(CaptureGatePure, SlackIsHalfTheTickIntervalCappedAtHalfTheMinimumInterval)
{
  EXPECT_NEAR(capture_gate_slack_seconds(kThirtyFps, kWindowsTick), kWindowsTick / 2.0, 1e-12);
  // A huge tick interval cannot remove the bound, only blur it by half.
  EXPECT_NEAR(capture_gate_slack_seconds(kThirtyFps, 0.1), kThirtyFps / 2.0, 1e-12);
}

TEST(CaptureGatePure, SlackIsZeroWhenTheTickIntervalIsUnknownOrThePanelUncapped)
{
  EXPECT_EQ(capture_gate_slack_seconds(kThirtyFps, 0.0), 0.0);
  EXPECT_EQ(capture_gate_slack_seconds(kThirtyFps, -1.0), 0.0);
  EXPECT_EQ(capture_gate_slack_seconds(0.0, kWindowsTick), 0.0);
}

TEST(CaptureGatePure, TheIdleThresholdNeverDropsBelowOneHundredMilliseconds)
{
  EXPECT_NEAR(capture_gate_idle_seconds(kThirtyFps), 0.1, 1e-12);
  EXPECT_NEAR(capture_gate_idle_seconds(1.0 / 15.0), 2.0 / 15.0, 1e-12);
  EXPECT_NEAR(capture_gate_idle_seconds(0.0), 0.1, 1e-12);
}

TEST(CaptureGatePure, ANonVideoPanelIsTheStrictLimiterBitForBit)
{
  for (const std::uint32_t max_fps : {15u, 30u, 60u}) {
    const double min_interval = 1.0 / static_cast<double>(max_fps);
    for (const double elapsed : {0.0, 0.004, 0.015625, 0.03125, min_interval - 1e-9,
        min_interval, min_interval + 1e-9, 0.5, 3.0})
    {
      CaptureGateInput in = after_a_capture(elapsed, max_fps);
      // Even a coarse tick interval must not loosen a non-video panel.
      in.tick_interval_seconds = kWindowsTick;
      const CaptureGateVerdict expected =
        elapsed < min_interval ? CaptureGateVerdict::kSkipPaced : CaptureGateVerdict::kCapture;
      EXPECT_EQ(decide_capture(in), expected) << "max_fps " << max_fps << " elapsed " << elapsed;
    }
  }
}

TEST(CaptureGatePure, TheFirstCaptureIsNeverHeldBack)
{
  CaptureGateInput in;
  in.now_seconds = 0.0;
  in.max_fps = 30;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
  in.frame_driven = true;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
}

TEST(CaptureGatePure, AnUncappedPanelCapturesEveryTickVideoOrNot)
{
  CaptureGateInput in = after_a_capture(0.0001, 0);
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
  in.frame_driven = true;  // no frame advanced, source not idle: still every tick
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
}

TEST(CaptureGatePure, ANewVideoFrameIsCapturedOnACoarseTickThatTheStrictLimiterWouldMiss)
{
  CaptureGateInput in = after_a_capture(2.0 * kWindowsTick, 30);  // 31.25 ms < 33.3 ms
  in.frame_driven = true;
  in.frame_advanced = true;

  // The strict rule: one tick too early, so a third tick (46.9 ms) would be needed.
  in.tick_interval_seconds = 0.0;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kSkipPaced);

  // With the observed 15.625 ms tick the 2 ms shortfall is inside the tolerance.
  in.tick_interval_seconds = kWindowsTick;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
}

TEST(CaptureGatePure, MaxFpsRemainsAnUpperBoundForVideo)
{
  CaptureGateInput in = after_a_capture(kWindowsTick, 30);  // 15.6 ms into a 33.3 ms interval
  in.frame_driven = true;
  in.frame_advanced = true;
  in.tick_interval_seconds = kWindowsTick;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kSkipPaced);

  // Even a wildly coarse tick only ever blurs the bound by half the interval.
  in.tick_interval_seconds = 0.1;
  in.now_seconds = 0.010;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kSkipPaced);
  in.now_seconds = 0.017;
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
}

TEST(CaptureGatePure, VideoWithNothingNewWaitsWhileTheSourceIsFlowingThenPollsStrictly)
{
  CaptureGateInput in = after_a_capture(0.05, 30);
  in.frame_driven = true;
  in.frame_advanced = false;
  in.tick_interval_seconds = 0.0003;

  in.seconds_since_frame = 0.05;  // a 30 fps source is never this quiet between frames
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kSkipNoNewFrame);

  in.seconds_since_frame = 0.12;  // quiet: back to the strict max_fps polling
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kCapture);
  in.now_seconds = 0.02;          // ...which still respects the interval
  EXPECT_EQ(decide_capture(in), CaptureGateVerdict::kSkipPaced);
}

TEST(TickIntervalEstimatorTest, ConvergesOnAConstantGapAndIsZeroUntilTwoTicks)
{
  TickIntervalEstimator estimator;
  EXPECT_EQ(estimator.interval_seconds(), 0.0);
  estimator.observe(0.0);
  EXPECT_EQ(estimator.interval_seconds(), 0.0);
  for (int n = 1; n <= 40; ++n) {
    estimator.observe(n * kWindowsTick);
  }
  EXPECT_NEAR(estimator.interval_seconds(), kWindowsTick, 1e-9);
}

TEST(TickIntervalEstimatorTest, AGuiStallIsABlipNotANewGranularity)
{
  TickIntervalEstimator estimator;
  double now = 0.0;
  for (int n = 0; n < 200; ++n) {
    estimator.observe(now);
    now += 0.0003;
  }
  const double before = estimator.interval_seconds();
  now += 2.0;  // a two-second stall
  estimator.observe(now);
  const double after = estimator.interval_seconds();
  // One clamped sample: at most kSmoothing * kMaxSampleSeconds of movement.
  EXPECT_LE(after - before,
    TickIntervalEstimator::kSmoothing * TickIntervalEstimator::kMaxSampleSeconds + 1e-9);
  // ...and it decays again within a few hundred ordinary ticks.
  for (int n = 0; n < 200; ++n) {
    now += 0.0003;
    estimator.observe(now);
  }
  EXPECT_LT(estimator.interval_seconds(), 0.001);
}

// ---------------------------------------------------------------------------
// QtPanelStream through the gate
// ---------------------------------------------------------------------------

TEST(FrameArrivalCapture, ThirtyFpsVideoKeepsItsRateOnWindowsDefaultTimerGranularity)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 320);  // 320x180, frames 64x36
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam", &widget, video_options(true), factory);

  const SimResult result = run_sim(stream, *source, 64, 36, 30.0, kWindowsTick, 2.0);

  EXPECT_TRUE(result.frame_driven);
  EXPECT_EQ(result.published, 60u);
  // Every published frame is captured on the first tick after it arrived and
  // reaches QarOS: ~30 fps, not the 21.3 fps a strict limiter collapses to.
  EXPECT_GE(result.captured, 59u);
  EXPECT_GE(result.submitted, 59u);
  // It is still bounded by max_fps: never more captures than frames.
  EXPECT_LE(result.captured, result.published + 1u);
}

TEST(FrameArrivalCapture, TheStrictLimiterStillCollapsesItWhenFrameArrivalIsSwitchedOff)
{
  // Pins the failure mode the gate prevents: 3 x 15.625 ms = 46.9 ms per capture.
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 320);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam", &widget, video_options(false), factory);

  const SimResult result = run_sim(stream, *source, 64, 36, 30.0, kWindowsTick, 2.0);

  EXPECT_FALSE(result.frame_driven);
  EXPECT_EQ(result.captured, 43u);  // 21.3 fps
}

TEST(FrameArrivalCapture, AVideoSourceWithNoNewFrameIsNotRepaintedWhileItIsFlowing)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 320);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("video:/cam", &widget, video_options(true), factory);

  source->publish(gray_frame(64, 36, 0));
  ASSERT_TRUE(stream.tick(0.0));
  for (int n = 1; n <= 50; ++n) {  // 1 kHz ticks for 50 ms, nothing published
    EXPECT_FALSE(stream.tick(n * 0.001));
  }
  EXPECT_EQ(stream.diagnostics().frames_captured, 1u);
  EXPECT_GT(stream.diagnostics().frames_skipped_no_new_frame, 0u);

  source->publish(gray_frame(64, 36, 1));
  EXPECT_TRUE(stream.tick(0.06));  // captured at once on the tick after it arrived
  EXPECT_EQ(stream.diagnostics().frames_captured, 2u);
}

TEST(FrameArrivalCapture, AStalledSourceFallsBackToStrictPollingSoTheKeepaliveStillFires)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 320);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = video_options(true);
  options.keepalive_seconds = 1.0;
  QtPanelStream stream("video:/cam", &widget, options, factory);

  source->publish(gray_frame(64, 36, 0));
  ASSERT_TRUE(stream.tick(0.0));
  for (int n = 1; n <= 200; ++n) {  // 10 ms ticks for 2 s, the source is silent
    stream.tick(n * 0.01);
  }
  // Polling resumed once the source had been quiet for 100 ms...
  EXPECT_GE(stream.diagnostics().frames_captured, 30u);
  // ...which is what lets the unchanged-frame keepalive resend happen.
  EXPECT_GE(factory->latest()->submit_calls(), 2u);
}

TEST(FrameArrivalCapture, ANonVideoPanelKeepsTheStrictLimiter)
{
  QWidget plain;
  plain.resize(200, 120);
  plain.show();
  plain.ensurePolished();
  QCoreApplication::processEvents();

  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("panel", &plain, video_options(true), factory);

  for (int n = 0; n < 128; ++n) {  // 2 s of ticks on the coarse grid
    stream.tick(n * kWindowsTick);
  }
  EXPECT_FALSE(stream.diagnostics().frame_driven);
  EXPECT_EQ(stream.diagnostics().frames_captured, 43u);  // every third tick: unchanged
  EXPECT_EQ(stream.diagnostics().frames_skipped_no_new_frame, 0u);
}

TEST(FrameArrivalCapture, ARegionSourceOnADockDrivesItToo)
{
  QWidget dock;
  dock.resize(400, 300);
  dock.setAutoFillBackground(true);
  QWidget * region = new QWidget(&dock);  // owned by `dock`
  region->setGeometry(0, 150, 400, 150);
  dock.show();
  dock.ensurePolished();
  QCoreApplication::processEvents();

  auto source = std::make_shared<LatestVideoFrame>();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dock:Camera", &dock, video_options(true), factory);
  stream.set_region_source(region, source);

  const SimResult result = run_sim(stream, *source, 400, 150, 30.0, kWindowsTick, 2.0);

  EXPECT_TRUE(result.frame_driven);
  EXPECT_GE(result.captured, 59u);
  EXPECT_GE(result.submitted, 59u);
}

TEST(SubmitGapWindow, MeasuresTheGapBetweenSubmittedFramesAndResets)
{
  auto source = std::make_shared<LatestVideoFrame>();
  VideoPanelWidget widget(source, QStringLiteral("cam"), 320);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  PanelStreamOptions options = video_options(true);
  options.max_fps = 0;  // unpaced: every tick with new content submits
  QtPanelStream stream("video:/cam", &widget, options, factory);

  source->publish(gray_frame(64, 36, 0));
  ASSERT_TRUE(stream.tick(0.000));  // the first submit has no predecessor
  EXPECT_EQ(stream.diagnostics().submit_gap_count, 0u);

  source->publish(gray_frame(64, 36, 1));
  ASSERT_TRUE(stream.tick(0.040));
  source->publish(gray_frame(64, 36, 2));
  ASSERT_TRUE(stream.tick(0.140));

  const auto & d = stream.diagnostics();
  EXPECT_EQ(d.submit_gap_count, 2u);
  EXPECT_NEAR(d.submit_gap_max_ms, 100.0, 1e-6);
  EXPECT_NEAR(d.submit_gap_sum_ms / static_cast<double>(d.submit_gap_count), 70.0, 1e-6);

  stream.reset_submit_gap_window();
  EXPECT_EQ(stream.diagnostics().submit_gap_count, 0u);
  EXPECT_EQ(stream.diagnostics().submit_gap_max_ms, 0.0);

  // The new window's first gap still spans the boundary (last submit was 0.140).
  source->publish(gray_frame(64, 36, 3));
  ASSERT_TRUE(stream.tick(0.160));
  EXPECT_EQ(stream.diagnostics().submit_gap_count, 1u);
  EXPECT_NEAR(stream.diagnostics().submit_gap_max_ms, 20.0, 1e-6);
}
