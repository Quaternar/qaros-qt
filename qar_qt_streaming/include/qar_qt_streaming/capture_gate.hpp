// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__CAPTURE_GATE_HPP_
#define QAR_QT_STREAMING__CAPTURE_GATE_HPP_

/// @file capture_gate.hpp
/// @brief The pure "may this panel capture on this tick?" decision.
///
/// ## Why this is not just `now - last < 1/max_fps`
///
/// A strict interval limiter is only as fine as the ticks it is evaluated on.
/// A 30 fps cap (33.33 ms) evaluated on ticks that land on Windows' default
/// 15.625 ms timer granularity needs THREE ticks (46.9 ms) to pass, because two
/// (31.25 ms) fall short by 2 ms: 30 fps collapses to 21.3 fps. A full-screen
/// QarOS Visualizer covering the host application triggers exactly this for the AR
/// video panels:
/// Windows stops honouring an occluded process's high timer-resolution request,
/// and every timer-driven cadence snaps back to the ~15.6 ms scheduler tick.
///
/// Two changes, both decided here so they can be tested without a widget, a
/// clock or a QApplication:
///
///  * a **frame-driven** panel (its content is a LatestVideoFrame: a
///    VideoPanelWidget's source, or a region source on a docked image view)
///    captures on the first tick after that source's sequence advanced, instead
///    of on a free-running timer-quantised schedule;
///  * `max_fps` stays as an UPPER BOUND, but is applied with a tolerance of half
///    the observed tick interval: a capture is allowed once
///    `1/max_fps - slack` has elapsed, so a coarse tick cannot push a
///    capture a whole extra tick late. The slack is capped at half the minimum
///    interval, so the bound can never be removed, only blurred.
///
/// A panel that is NOT frame-driven (a control panel: text and buttons) keeps
/// the strict limiter bit for bit. A frame-driven panel whose source has gone
/// quiet falls back to that same strict polling, which is what keeps the
/// keepalive resend, a status-text change and a popup working while no video
/// is arriving.

#include <cstdint>

namespace qar_qt_streaming
{

/// @brief What the gate decided for one tick.
enum class CaptureGateVerdict
{
  kCapture,          ///< Go ahead: repaint the widget and run the dirty check.
  kSkipPaced,        ///< Inside the max_fps interval (the upper bound).
  kSkipNoNewFrame    ///< Frame-driven, nothing new has arrived, and the source is not idle.
};

/// @brief Everything the decision needs, as plain numbers.
struct CaptureGateInput
{
  /// Monotonic seconds, as given to QtPanelStream::tick.
  double now_seconds = 0.0;

  /// A capture has been let through before, so @ref last_capture_seconds is valid.
  bool has_captured = false;
  double last_capture_seconds = 0.0;

  /// The panel's cap; 0 = uncapped (every tick may capture).
  std::uint32_t max_fps = 0;

  /// Smoothed gap between ticks in seconds (see @ref TickIntervalEstimator).
  /// 0 = not known yet, which means no tolerance.
  double tick_interval_seconds = 0.0;

  /// The content has at least one live LatestVideoFrame source.
  bool frame_driven = false;

  /// A source published since the last capture let through.
  bool frame_advanced = false;

  /// Seconds since a source was last seen to publish. Meaningful when
  /// @ref frame_driven.
  double seconds_since_frame = 0.0;
};

/// @brief The tolerance applied to the max_fps interval: half the tick interval,
/// never more than half the interval itself, never negative.
double capture_gate_slack_seconds(double min_interval_seconds, double tick_interval_seconds);

/// @brief How long a frame-driven source may stay silent before the panel goes
/// back to polling at the strict max_fps rate: two capture intervals, but never
/// less than 100 ms. Long enough that a 30 fps source is never "quiet" between
/// two frames (polling in that gap would capture unchanged pixels and push the
/// real frame a whole interval late), short enough that a stalled feed's status
/// text and keepalive are noticed at once.
double capture_gate_idle_seconds(double min_interval_seconds);

/// @brief Decide whether this tick may capture.
CaptureGateVerdict decide_capture(const CaptureGateInput & input);

/// @brief Smoothed estimate of the gap between successive ticks.
///
/// Fed the time of EVERY tick the stream is given, including the ones the gate
/// then sheds: those are exactly the ticks that reveal the timer's granularity.
/// An exponential moving average with each sample clamped to
/// @ref kMaxSampleSeconds, so one GUI stall is a blip rather than a new
/// granularity.
class TickIntervalEstimator
{
public:
  static constexpr double kSmoothing = 0.2;
  static constexpr double kMaxSampleSeconds = 0.1;

  /// @brief Record a tick at @p now_seconds (monotonic).
  void observe(double now_seconds);

  /// @brief Smoothed gap in seconds; 0 until two ticks have been seen.
  double interval_seconds() const { return interval_seconds_; }

private:
  bool has_last_ = false;
  bool has_interval_ = false;
  double last_seconds_ = 0.0;
  double interval_seconds_ = 0.0;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__CAPTURE_GATE_HPP_
