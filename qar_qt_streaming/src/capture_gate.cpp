// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/capture_gate.hpp"

#include <algorithm>

namespace qar_qt_streaming
{

double
capture_gate_slack_seconds(double min_interval_seconds, double tick_interval_seconds)
{
  if (min_interval_seconds <= 0.0 || tick_interval_seconds <= 0.0) {
    return 0.0;
  }
  return std::min(0.5 * tick_interval_seconds, 0.5 * min_interval_seconds);
}

double
capture_gate_idle_seconds(double min_interval_seconds)
{
  return std::max(0.1, 2.0 * min_interval_seconds);
}

CaptureGateVerdict
decide_capture(const CaptureGateInput & input)
{
  const double min_interval =
    input.max_fps > 0 ? 1.0 / static_cast<double>(input.max_fps) : 0.0;
  const double elapsed = input.now_seconds - input.last_capture_seconds;

  // Not frame-driven, or uncapped (an uncapped panel already captures on every
  // tick, which is a superset of "on the first tick after a frame arrived"):
  // the plain strict limiter.
  if (!input.frame_driven || input.max_fps == 0) {
    if (input.max_fps > 0 && input.has_captured && elapsed < min_interval) {
      return CaptureGateVerdict::kSkipPaced;
    }
    return CaptureGateVerdict::kCapture;
  }

  // Nothing has been shown yet: the first capture is never held back.
  if (!input.has_captured) {
    return CaptureGateVerdict::kCapture;
  }

  if (input.frame_advanced) {
    // Capture on arrival; max_fps is only an upper bound, blurred by the tick
    // granularity so a coarse tick cannot cost a whole extra tick.
    const double slack = capture_gate_slack_seconds(min_interval, input.tick_interval_seconds);
    return elapsed + slack >= min_interval ? CaptureGateVerdict::kCapture
                                           : CaptureGateVerdict::kSkipPaced;
  }

  // No new frame. While the source is flowing there is nothing to repaint for;
  // once it has been quiet for a while, poll at the strict rate again so the
  // keepalive, a status-text change or a popup is still noticed.
  if (input.seconds_since_frame >= capture_gate_idle_seconds(min_interval)) {
    return elapsed >= min_interval ? CaptureGateVerdict::kCapture
                                   : CaptureGateVerdict::kSkipPaced;
  }
  return CaptureGateVerdict::kSkipNoNewFrame;
}

void
TickIntervalEstimator::observe(double now_seconds)
{
  if (has_last_) {
    const double gap = std::min(now_seconds - last_seconds_, kMaxSampleSeconds);
    if (gap > 0.0) {
      interval_seconds_ = has_interval_
        ? (1.0 - kSmoothing) * interval_seconds_ + kSmoothing * gap
        : gap;
      has_interval_ = true;
    }
  }
  last_seconds_ = now_seconds;
  has_last_ = true;
}

}  // namespace qar_qt_streaming
