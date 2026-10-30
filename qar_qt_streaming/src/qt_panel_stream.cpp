// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qt_panel_stream.hpp"

#include "qar_qt_streaming/frame_luminance.hpp"
#include "qar_qt_streaming/panel_hit_test.hpp"
#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace qar_qt_streaming
{

namespace
{

/// How a capture tick that passed the gate ended. The numbers index kOutcomeWords, which is
/// what the `outcome` column of video_<key>.csv prints.
enum TickOutcome : std::int64_t
{
  kOutcomeSubmitted = 0,
  kOutcomeNotReady = 1,        ///< try_begin_frame said QarOS has no destination ready
  kOutcomeUnchanged = 2,       ///< nothing new to send (the dirty check)
  kOutcomeCaptureFailed = 3,
  kOutcomeSubmitFailed = 4,
  kOutcomeAdapterError = 5,    ///< geometry / sender creation / try_begin_frame failed
};

const char * const kOutcomeWords[] = {
  "submitted", "not_ready", "unchanged", "capture_failed", "submit_failed", "adapter_error"};

const char * const kTimingColumns[] = {
  "seq", "header_stamp_us", "arrival_us", "decode_done_us", "mailbox_us", "decode_seq",
  "tick_us", "capture_us", "ready_us", "not_ready_start_us", "not_ready_end_us",
  "not_ready_count", "submit_us", "outcome"};
constexpr int kTimingOutcomeColumn = 13;

/// One row of video_<key>.csv, written when it goes out of scope so that EVERY way out of
/// QtPanelStream::tick() - there are a dozen - produces exactly one. Inert (a null test) when
/// timing is off. The row is the whole life of one captured frame: where it came from
/// (the VideoFrameTrace the decode worker stored with it), when this tick picked it up, when
/// the capture finished, whether QarOS was ready, when it was submitted, and how it ended.
struct TickTiming
{
  qar_ar_core::TimingSink * sink = nullptr;
  VideoFrameTrace frame{};
  std::uint64_t decode_seq = 0;
  std::int64_t tick_us = qar_ar_core::kTimingBlank;
  std::int64_t capture_us = qar_ar_core::kTimingBlank;
  std::int64_t ready_us = qar_ar_core::kTimingBlank;
  std::int64_t not_ready_start_us = qar_ar_core::kTimingBlank;
  std::int64_t not_ready_end_us = qar_ar_core::kTimingBlank;
  std::int64_t not_ready_count = qar_ar_core::kTimingBlank;
  std::int64_t submit_us = qar_ar_core::kTimingBlank;
  std::int64_t outcome = kOutcomeAdapterError;

  TickTiming() = default;
  TickTiming(const TickTiming &) = delete;
  TickTiming & operator=(const TickTiming &) = delete;

  ~TickTiming()
  {
    if (sink == nullptr) {
      return;
    }
    const bool known = frame.valid;
    sink->append({
        known ? static_cast<std::int64_t>(frame.seq) : qar_ar_core::kTimingBlank,
        known ? frame.header_stamp_us : qar_ar_core::kTimingBlank,
        known ? frame.arrival_us : qar_ar_core::kTimingBlank,
        known ? frame.decode_done_us : qar_ar_core::kTimingBlank,
        known ? frame.mailbox_us : qar_ar_core::kTimingBlank,
        static_cast<std::int64_t>(decode_seq),
        tick_us, capture_us, ready_us, not_ready_start_us, not_ready_end_us, not_ready_count,
        submit_us, outcome});
  }
};

/// The geometry input of @p widget as it is now: Qt's logical size and pixel ratio with the
/// sizing policy of @p options. The stream and ar_panel_height_m() both build it here, so that
/// the height a host predicts is the height the stream sends.
PanelGeometryInput
geometry_input_for(const QWidget & widget, const PanelStreamOptions & options)
{
  PanelGeometryInput input;
  input.logical_width = static_cast<std::uint32_t>(widget.width() > 0 ? widget.width() : 0);
  input.logical_height = static_cast<std::uint32_t>(widget.height() > 0 ? widget.height() : 0);
  input.device_pixel_ratio = widget.devicePixelRatioF();
  input.meters_per_logical_px = options.meters_per_logical_px;
  // The frame of QarOS is made room for in the height of a panel and in the width of a dialog.
  input.frame = options.ar_frame;
  input.narrow_for_frame = options.dialog_panel;
  input.render_scale = options.render_scale;
  input.buffer_alignment = options.buffer_alignment;
  input.max_buffer_dimension = options.max_buffer_dimension;
  return input;
}

/// The tail of the size line of the panel of a dialog: the width the widget asks for, the width
/// that is sent, and, if the width is not the one the frame asks for, why.
std::string
narrowing_note(const PanelGeometry & geometry)
{
  std::string note = " (a dialog: width " + std::to_string(geometry.nominal_width_meters) +
    " -> " + std::to_string(geometry.width_meters) + " m for the frame";
  if (geometry.narrowing == FrameNarrowing::Limited) {
    note += ", limited to half of the width";
  } else if (geometry.narrowing == FrameNarrowing::Skipped) {
    note += ", not applied because the frame and its fit margin are not below the video height";
  } else if (geometry.narrowing == FrameNarrowing::None) {
    note += ", not made wider than the widget asks";
  }
  return note + ")";
}

/// The part of the size line that names the video and the frame it is drawn inside: the four
/// insets and the fit margin, which is the height the panel has beyond its video and the insets.
std::string
frame_note(const PanelGeometry & geometry)
{
  return " (video " + std::to_string(geometry.video_width_meters) + " x " +
         std::to_string(geometry.video_height_meters) + " m inside a frame of top " +
         std::to_string(geometry.frame.top_m) + ", bottom " +
         std::to_string(geometry.frame.bottom_m) + ", left " +
         std::to_string(geometry.frame.left_m) + ", right " +
         std::to_string(geometry.frame.right_m) + " m, fit margin " +
         std::to_string(geometry.frame.fit_margin_m) + " m";
}

}  // namespace

void
apply_readability(const ReadabilityRecommendation & recommendation, PanelStreamOptions & options)
{
  options.meters_per_logical_px = recommendation.meters_per_logical_px;
  options.render_scale = recommendation.render_scale;
}

float
ar_panel_height_m(const QWidget & widget, const PanelStreamOptions & options)
{
  PanelGeometry geometry;
  std::string error;
  if (!compute_panel_geometry(geometry_input_for(widget, options), geometry, error)) {
    return 0.0f;
  }
  return geometry.height_meters;
}

QtPanelStream::QtPanelStream(
  std::string key,
  QWidget * widget,
  PanelStreamOptions options,
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory)
: key_(std::move(key)),
  widget_(widget),
  options_(std::move(options)),
  factory_(std::move(factory)),
  capture_(widget)
{
  placement_ = options_.placement;
  diagnostics_.key = key_;
  diagnostics_.display_name = options_.display_name.empty() ? key_ : options_.display_name;
  if (options_.input.enabled && widget != nullptr) {
    injector_ = std::make_unique<QtPanelInputInjector>(widget, options_.input);
  }
}

QtPanelStream::~QtPanelStream()
{
  close();
}

void
QtPanelStream::tombstone(const PanelTombstoneText & text)
{
  if (diagnostics_.tombstoned) {
    return;  // already a notice; the first reason is the true one
  }

  // The injector goes first and unconditionally: the widget it injects into may
  // ALREADY be destroyed (this is called from the QPointer sweep that notices
  // exactly that), and a notice has nothing to click in any case.
  if (injector_) {
    injector_->reset();
    sync_input_diagnostics();
    injector_.reset();
  }

  // Frozen at the size the LIVE sender was built for. refresh_geometry() is
  // skipped from here on, so the notice is captured into the same buffer layout
  // the adapter already has and a tombstone can never be the reason a sender is
  // rebuilt — which would be the very teardown this whole mechanism avoids.
  const std::uint32_t width = logical_width_;
  const std::uint32_t height = logical_height_;

  PanelTombstoneStyle style;
  // The panel's own readability carries over unchanged, and that is the whole
  // sizing argument: `meters_per_logical_px` was derived (ar_readability.hpp)
  // so that a body_font_px-tall glyph subtends a comfortable ~22 arc-minutes,
  // and the notice is laid out in those SAME logical pixels at that same
  // metres-per-pixel. So body text here reads exactly as body text read in the
  // window this replaces, and the only number that has to be looked up is the
  // floor below which the fit may not shrink it.
  ReadabilityProfile profile;
  ReadabilityRecommendation readability;
  std::string readability_error;
  style.body_font_px = profile.body_font_px;
  if (recommend_readability(profile, readability, readability_error)) {
    style.minimum_font_px = readability.min_legible_font_px;
  }

  tombstone_widget_.reset(make_panel_tombstone_widget(text, style, width, height));
  if (!tombstone_widget_) {
    record_error("could not build the panel-closed notice");
    return;
  }
  widget_ = tombstone_widget_.get();
  capture_.set_widget(widget_.data());
  // The census describes the window that is gone; a second one of it would describe the notice.
  census_follow_up_armed_ = false;

  // A stream that was parked and could not be hidden ends here as a notice, and is not parked.
  diagnostics_.parked = false;
  park_notice_reason_.clear();
  diagnostics_.tombstoned = true;
  diagnostics_.tombstone_reason =
    text.reason.empty() ? std::string(kTombstoneDefaultReason) : text.reason;
  diagnostics_.tombstone_clipped = tombstone_is_clipped(
    width,
    static_cast<std::size_t>(
      (text.panel_name.empty() ? std::string(kTombstoneAnonymousName) : text.panel_name).size()),
    style);
  // Nothing about the notice is "unchanged" relative to the window it replaces,
  // and the resize timer has nothing left to protect.
  clear_pending_resize();
}

void
QtPanelStream::suspend_capture()
{
  if (diagnostics_.ar_panel_closed) {
    return;
  }
  diagnostics_.ar_panel_closed = true;
  if (injector_) {
    // Whatever was pressed on this panel can never be released through it now.
    injector_->reset();
    sync_input_diagnostics();
  }
  // NOTE: the adapter is NOT touched. The panel is closed in AR, but that is not
  // the Qt window closing, and a receiver may still be attached to the stream:
  // its sender is destroyed once, by the Qt-side close or at session shutdown.
  // Destroying it here would put that receiver into `reconnecting`, which on a
  // runtime that does not survive that (PanelCloseMode::Notice) starves every
  // other stream the QarOS mixer serves (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy order").
}

void
QtPanelStream::request_ar_visible(bool visible)
{
  visibility_requested_ = true;
  requested_visible_ = visible;
}

void
QtPanelStream::record_visibility_failure(std::string message)
{
  ++diagnostics_.visibility_failures;
  diagnostics_.last_visibility_error = std::move(message);
}

void
QtPanelStream::apply_visibility_request()
{
  if (!visibility_requested_ || !adapter_) {
    return;  // nothing asked, or no sender yet: ensure_adapter() comes back here
  }
  visibility_requested_ = false;
  const bool visible = requested_visible_;

  // Already as asked: shown and asked to be shown, or hidden and asked to be hidden. A panel
  // the operator closed in AR is neither, so showing it is tried.
  const bool shown_now = !diagnostics_.ar_hidden;
  if (visible == shown_now && !(visible && diagnostics_.ar_panel_closed)) {
    return;
  }

  if (!adapter_->supports_state()) {
    record_visibility_failure("the panel sender cannot change the state of a panel");
    return;
  }
  if (!adapter_->set_state(visible ? QAR_GUI_PANEL_STATE_VISIBLE : QAR_GUI_PANEL_STATE_HIDDEN)) {
    record_visibility_failure(
      adapter_->last_error().empty() ? std::string("set_state was refused") :
      adapter_->last_error());
    return;
  }

  // Accepted: a sender that queues the call may still refuse it, and then the result undoes this.
  StateAsk ask;
  ask.changes_flag = true;
  ask.previous_hidden = diagnostics_.ar_hidden;
  ask.revival = revival_pending_;
  note_state_ask(ask);
  ++diagnostics_.visibility_changes;
  diagnostics_.last_visibility_error.clear();
  diagnostics_.ar_hidden = !visible;
  if (visible) {
    // Back from hidden, or from a close by the operator, which QarOS has just undone: the
    // receiver has seen no frame for a while and needs one even if the widget has not changed,
    // and the time spent away is not time a receiver spent waiting for a first frame.
    diagnostics_.ar_panel_closed = false;
    capture_.invalidate();
    has_unserved_since_ = false;
    diagnostics_.unserved_seconds = 0.0;
  }
}

void
QtPanelStream::park(std::string notice_reason)
{
  if (diagnostics_.parked || diagnostics_.tombstoned) {
    return;  // already out of use; the first park stands
  }
  // The injector goes first and unconditionally: the window it injects into may ALREADY be
  // destroyed (the sweep that parks a stream is the one that notices exactly that), and a
  // parked panel takes no input in any case.
  if (injector_) {
    injector_->reset();
    sync_input_diagnostics();
    injector_.reset();
  }
  // The capture lets go of the window, so one destroyed while the stream is parked leaves
  // nothing dangling in it. widget_ is guarded and keeps the window for as long as it lives.
  capture_.set_widget(nullptr);
  clear_pending_resize();
  census_follow_up_armed_ = false;  // nothing is shown, so there is nothing left to measure

  park_notice_reason_ = std::move(notice_reason);
  park_failures_seen_ = diagnostics_.visibility_failures;
  revival_pending_ = false;  // a show asked for and not yet carried out is overtaken by the hide
  diagnostics_.parked = true;
  if (diagnostics_.ar_panel_closed) {
    // The operator took the panel down in AR, so it is out of sight already and QarOS is asked
    // for nothing. A later revival tries to show it, as showing a panel closed in AR does.
    visibility_requested_ = false;
    diagnostics_.ar_hidden = true;
  } else {
    request_ar_visible(false);
  }
}

bool
QtPanelStream::park_hide_refused() const
{
  return diagnostics_.parked && !diagnostics_.ar_hidden && !visibility_requested_ &&
         diagnostics_.visibility_failures > park_failures_seen_;
}

bool
QtPanelStream::revive_parked(QWidget * widget)
{
  if (!diagnostics_.parked) {
    return false;
  }
  QWidget * const target = widget != nullptr ? widget : widget_.data();
  if (target == nullptr) {
    return false;  // its window is gone and no other was given
  }
  widget_ = target;
  capture_.set_widget(target);
  diagnostics_.parked = false;
  park_notice_reason_.clear();
  if (options_.input.enabled) {
    injector_ = std::make_unique<QtPanelInputInjector>(target, options_.input);
  }
  // The committed size is still the one the live sender was built for, deliberately: a window
  // of the same size (the same dock or dialog opened again) needs no rebuild at all, and a
  // differently sized one goes through the ordinary resize hysteresis. Only the timing is
  // restarted, so a size the previous window was part-way through settling on does not count.
  clear_pending_resize();
  last_desired_buffer_width_ = 0;
  last_desired_buffer_height_ = 0;
  diagnostics_.last_error.clear();
  // The window shown now is measured from its first frame on, like a new stream.
  census_pair_pending_ = true;
  census_follow_up_armed_ = false;
  // Shown by the next tick; a panel the operator closed in AR is tried again there.
  revival_pending_ = true;
  revival_failures_seen_ = diagnostics_.visibility_failures;
  request_ar_visible(true);
  return true;
}

bool
QtPanelStream::take_revival_refusal()
{
  if (late_revival_refusal_) {
    late_revival_refusal_ = false;  // the sender accepted the show and refused it afterwards
    revival_pending_ = false;
    return true;
  }
  if (!revival_pending_ || visibility_requested_) {
    return false;  // no revival waiting for its verdict, or the show is still to be carried out
  }
  revival_pending_ = false;
  return diagnostics_.visibility_failures > revival_failures_seen_;
}

std::vector<std::string>
QtPanelStream::take_notes()
{
  std::vector<std::string> taken;
  taken.swap(notes_);
  return taken;
}

std::vector<std::string>
QtPanelStream::take_warnings()
{
  std::vector<std::string> taken;
  taken.swap(warnings_);
  return taken;
}

void
QtPanelStream::request_scroll_census(std::string reason)
{
  census_requested_ = true;
  census_request_reason_ = std::move(reason);
}

void
QtPanelStream::say_note(std::string line)
{
  constexpr std::size_t kMaxPendingLines = 24;  // a stream nobody drains must not grow
  if (notes_.size() >= kMaxPendingLines) {
    notes_.erase(notes_.begin());
  }
  notes_.push_back(std::move(line));
}

void
QtPanelStream::say_warning(std::string line)
{
  constexpr std::size_t kMaxPendingLines = 24;
  if (warnings_.size() >= kMaxPendingLines) {
    warnings_.erase(warnings_.begin());
  }
  warnings_.push_back(std::move(line));
}

void
QtPanelStream::note_brightness(double now_seconds)
{
  // One sparse pass a second is the whole cost; the epsilon keeps a clock that adds up tenths
  // from missing the second by a rounding error.
  constexpr double kBrightnessIntervalSeconds = 1.0;
  if (brightness_measured_ &&
    now_seconds - brightness_seconds_ < kBrightnessIntervalSeconds - 1e-9)
  {
    return;
  }
  // The frame that was just submitted is still in the capture buffer, so this reads it in place.
  const FrameBrightness brightness = frame_brightness(capture_.pixels());
  diagnostics_.luminance_all = brightness.all;
  diagnostics_.luminance_top = brightness.top;
  diagnostics_.luminance_rest = brightness.rest;
  diagnostics_.alpha_mean = brightness.alpha;
  diagnostics_.luminance_frame = diagnostics_.frames_submitted;
  brightness_measured_ = true;
  brightness_seconds_ = now_seconds;
}

void
QtPanelStream::take_census(const std::string & when, bool follow_up)
{
  QWidget * const root = widget_.data();
  if (root == nullptr) {
    return;  // the window is gone
  }
  const ScrollCensus census = take_scroll_census(*root);
  say_note(
    "'" + key_ + "' scroll census (" + when + "): " + std::to_string(census.areas.size()) +
    " area(s), " + std::to_string(census.hidden_areas) + " hidden: " +
    describe_scroll_census(census));
  if (!follow_up) {
    return;
  }
  // A bar that is still there this long after the first frame stays for good, so it is said
  // once for each area: as a Warning when the area was not meant to scroll.
  for (const ScrollAreaCensus & area : census.areas) {
    if (!area.v_bar) {
      continue;
    }
    const long percent = area.viewport_h > 0 ?
      std::lround(100.0 * area.overflow_px / area.viewport_h) : 0L;
    const std::string what =
      area.class_name + " '" + area.object_name + "' overflows by " +
      std::to_string(area.overflow_px) + " px (" + std::to_string(percent) + " % of its " +
      std::to_string(area.viewport_h) + " px viewport)";
    if (area.by_design) {
      say_note(
        "'" + key_ + "' 1.5 s after its first frame an area that scrolls by design shows a "
        "vertical scroll bar: " + what);
    } else {
      say_warning(
        "'" + key_ + "' still shows a vertical scroll bar 1.5 s after its first frame: " + what);
    }
  }
}

void
QtPanelStream::note_scroll_census(double now_seconds)
{
  if (census_pair_pending_) {
    census_pair_pending_ = false;
    take_census("first frame", false);
    census_follow_up_armed_ = true;
    census_follow_up_seconds_ = now_seconds + kCensusFollowUpSeconds;
  }
  if (census_requested_) {
    census_requested_ = false;
    const std::string reason =
      census_request_reason_.empty() ? std::string("requested") : census_request_reason_;
    census_request_reason_.clear();
    take_census(reason, false);
  }
}

void
QtPanelStream::note_submitted_frame(std::uint64_t unserved_ticks, double now_seconds)
{
  note_brightness(now_seconds);
  if (burst_remaining_ > 0) {
    --burst_remaining_;
  }
  const bool after_streak = unserved_ticks >= kBurstStreakTicks;
  if (after_streak) {
    burst_remaining_ = kBurstFrames - 1;  // this frame is the first of the burst
  }
  const bool first = !first_frame_noted_;
  first_frame_noted_ = true;
  if (first || after_streak) {
    // The frame that was just submitted is still in the capture buffer, so this reads it in
    // place.
    char level[32] = {0};
    std::snprintf(level, sizeof(level) - 1, "%.1f", mean_luminance(capture_.pixels()));
    const std::string head = "'" + key_ + "' frame luminance " + level;
    if (first) {
      say_note(head + " (first frame)");
    }
    if (after_streak) {
      say_note(head + " (first frame after " + std::to_string(unserved_ticks) + " unserved ticks)");
    }
  }
  note_scroll_census(now_seconds);
}

bool
QtPanelStream::revive(QWidget * widget)
{
  if (!diagnostics_.tombstoned || widget == nullptr) {
    return false;
  }
  widget_ = widget;
  capture_.set_widget(widget_.data());
  tombstone_widget_.reset();
  diagnostics_.tombstoned = false;
  diagnostics_.tombstone_reason.clear();
  diagnostics_.tombstone_clipped = false;
  diagnostics_.ar_panel_closed = false;
  // A stream that was hidden when its window closed shows its new window again.
  request_ar_visible(true);
  if (options_.input.enabled) {
    injector_ = std::make_unique<QtPanelInputInjector>(widget_.data(), options_.input);
  }
  // Geometry is live again, and the committed size is still the one the LIVE
  // sender was built for — deliberately. A revived widget of the same size (the
  // ordinary case: the same dock or the same dialog re-opened) therefore needs
  // no rebuild at all, and a differently-sized one goes through the ordinary
  // resize hysteresis rather than tearing the sender down on the first tick.
  // Only the TIMING is restarted, so a size the dead window happened to be
  // part-way through settling on does not count towards the new one's.
  clear_pending_resize();
  last_desired_buffer_width_ = 0;
  last_desired_buffer_height_ = 0;
  diagnostics_.last_error.clear();
  census_pair_pending_ = true;
  census_follow_up_armed_ = false;
  return true;
}

qar_ar_core::PanelCloseReport
QtPanelStream::close()
{
  qar_ar_core::PanelCloseReport report;
  if (injector_) {
    // The panel is going away under any pointer still pressed on it; release
    // the widgets before the QarOS side, so nothing stays stuck down.
    injector_->reset();
    sync_input_diagnostics();
  }
  if (adapter_) {
    // If the operator already closed this panel from the AR side the adapter must
    // not close it again: a panel is closed once, by whoever got there first.
    report = adapter_->close(diagnostics_.ar_panel_closed);
    adapter_.reset();
  }
  // The panels that size rebuilds replaced and kept (set_replaced_panel_policy) go after the
  // current one, each exactly once. The input subscription of the key belongs to the current
  // adapter, and a close cancels the subscription of its key, so the current one goes first.
  for (const KeptPanel & kept : replaced_adapters_) {
    kept.adapter->close(false);
  }
  replaced_adapters_.clear();
  // No adapter means nothing to protect from a resize any more; a restart must
  // adopt whatever size the widget has by then, immediately.
  clear_pending_resize();
  diagnostics_.active = false;
  // The notice widget is deliberately NOT released here. close() runs from the
  // destructor, where the member is about to be destroyed anyway, and leaving
  // it alive keeps widget() valid for the caller's own teardown bookkeeping
  // (attribution unregister, the retirement line) instead of handing it a
  // dangling pointer at the one moment it is least expecting one.
  return report;
}

void
QtPanelStream::set_pose(const QarPose & pose)
{
  placement_.pose = pose;
  options_.placement.pose = pose;
  if (adapter_ && !adapter_->set_placement(placement_)) {
    record_error(adapter_->last_error());
  }
}

void
QtPanelStream::set_pose_on_next_tick(const QarPose & pose)
{
  placement_.pose = pose;
  options_.placement.pose = pose;
  pose_pending_ = true;
}

void
QtPanelStream::apply_pending_pose()
{
  if (!pose_pending_ || !adapter_) {
    return;  // nothing asked, or no sender yet: ensure_adapter() builds it with the pose
  }
  pose_pending_ = false;
  if (!adapter_->set_placement(placement_)) {
    record_error(adapter_->last_error());
  }
}

void
QtPanelStream::note_state_ask(const StateAsk & ask)
{
  constexpr std::size_t kMaxOpenAsks = 32;
  if (state_asks_.size() >= kMaxOpenAsks) {
    state_asks_.pop_front();
  }
  state_asks_.push_back(ask);
}

void
QtPanelStream::drain_adapter_results(double now_seconds)
{
  std::vector<qar_ar_core::PanelOpResult> results;
  if (adapter_) {
    adapter_->drain_results(results);
  }
  for (const qar_ar_core::PanelOpResult & result : results) {
    if (!adapter_) {
      break;  // the sender was dropped by an earlier result; the rest was about it
    }
    switch (result.op) {
      case qar_ar_core::PanelOpResult::Op::Create:
        if (result.ok) {
          diagnostics_.stream_id = adapter_->stream_id_text();
        } else {
          // What a null sender from the factory does: the error is kept, the stream is inactive,
          // and the next tick after a pause makes a sender again.
          record_error(result.error.empty() ? std::string("panel sender creation failed")
                                            : result.error);
          diagnostics_.active = false;
          adapter_.reset();
          state_asks_.clear();
          has_create_retry_ = true;
          create_retry_at_seconds_ = now_seconds + kCreateRetrySeconds;
        }
        break;
      case qar_ar_core::PanelOpResult::Op::Placement:
        if (!result.ok) {
          record_error(result.error.empty() ? std::string("set_placement was refused")
                                            : result.error);
        }
        break;
      case qar_ar_core::PanelOpResult::Op::State: {
          StateAsk ask;
          if (!state_asks_.empty()) {
            ask = state_asks_.front();
            state_asks_.pop_front();
          }
          if (result.ok) {
            break;
          }
          record_visibility_failure(
            result.error.empty() ? std::string("set_state was refused") : result.error);
          if (ask.changes_flag) {
            diagnostics_.ar_hidden = ask.previous_hidden;
            if (diagnostics_.visibility_changes > 0) {
              --diagnostics_.visibility_changes;
            }
          }
          if (ask.revival) {
            late_revival_refusal_ = true;
          }
          break;
        }
      default:
        break;  // navigation and visibility lists belong to browser panels
    }
  }

  // The panels that size rebuilds kept: a hide that was accepted and then refused is asked for
  // again like one that was refused at once.
  for (KeptPanel & kept : replaced_adapters_) {
    results.clear();
    kept.adapter->drain_results(results);
    for (const qar_ar_core::PanelOpResult & result : results) {
      if (result.op != qar_ar_core::PanelOpResult::Op::State || result.ok ||
        result.state != QAR_GUI_PANEL_STATE_HIDDEN)
      {
        continue;
      }
      kept.hidden = false;
      kept.retries_left =
        kept.attempts <= kKeptHideRetries ? kKeptHideRetries + 1 - kept.attempts : 0;
      kept.next_retry_seconds = now_seconds + kKeptHideRetrySeconds;
      const std::string which =
        "'" + key_ + "' the old QarOS panel" +
        (kept.adapter->stream_id_text().empty() ? std::string() :
        " (stream " + kept.adapter->stream_id_text() + ")") +
        " that a size change replaced";
      if (kept.retries_left == 0) {
        say_warning(
          which + " is still visible after " + std::to_string(kept.attempts) +
          " attempts to hide it; it stays visible and is kept until the session ends");
      } else {
        say_note(
          which + " could not be hidden (" +
          (result.error.empty() ? std::string("no reason given") : result.error) +
          "); it is asked again");
      }
    }
  }
}

void
QtPanelStream::record_error(std::string message)
{
  diagnostics_.last_error = std::move(message);
}

void
QtPanelStream::sync_input_diagnostics()
{
  if (!injector_) {
    return;
  }
  const InputInjectionStats & stats = injector_->stats();
  diagnostics_.input_events_delivered = stats.delivered;
  diagnostics_.input_events_dropped = stats.dropped + hidden_input_drops_;
  // The injector's reason stands unless the most recent drop was ours (the panel was hidden).
  if (hidden_input_drops_ == 0 || stats.dropped != injector_drops_seen_) {
    diagnostics_.last_input_drop_reason = stats.last_drop_reason;
  }
  injector_drops_seen_ = stats.dropped;
}

bool
QtPanelStream::deliver_input(const PointerAction & action)
{
  if (diagnostics_.ar_hidden) {
    // Nobody can point at a panel that is hidden in AR, so whatever arrives for it is stale.
    ++hidden_input_drops_;
    ++diagnostics_.input_events_dropped;
    diagnostics_.last_input_drop_reason = "hidden in AR";
    return false;
  }
  if (!injector_) {
    ++diagnostics_.input_events_dropped;
    diagnostics_.last_input_drop_reason = "input is disabled for this panel";
    return false;
  }
  // Counted for exactly as long as the widget's handler can be running inside
  // this call. See delivering(): it is what lets the manager defer destroying
  // this stream when that handler is the one asking for the close.
  struct InFlight
  {
    explicit InFlight(int & count) : count_(count) { ++count_; }
    ~InFlight() { --count_; }
    InFlight(const InFlight &) = delete;
    InFlight & operator=(const InFlight &) = delete;
    int & count_;
  } in_flight(deliveries_in_flight_);
  const bool delivered = injector_->deliver(action);
  sync_input_diagnostics();
  return delivered;
}

std::uint64_t
QtPanelStream::frame_sequence(bool & driven) const
{
  std::uint64_t sum = capture_.region_frame_sequence(driven);
  // Variant (a): the widget IS the video, painting straight from its mailbox.
  if (const auto * video = dynamic_cast<const VideoPanelWidget *>(widget_.data())) {
    if (video->source()) {
      driven = true;
      sum += video->source()->sequence();
    }
  }
  return sum;
}

VideoFrameTrace
QtPanelStream::newest_frame_trace() const
{
  VideoFrameTrace best;
  capture_.newest_region_frame_trace(best);
  if (const auto * video = dynamic_cast<const VideoPanelWidget *>(widget_.data())) {
    if (video->source()) {
      const VideoFrameTrace own = video->source()->latest_trace();
      if (own.valid && (!best.valid || own.mailbox_us > best.mailbox_us)) {
        best = own;
      }
    }
  }
  return best;
}

qar_ar_core::TimingSink *
QtPanelStream::timing_sink()
{
  if (!timing_probed_) {
    timing_probed_ = true;
    qar_ar_core::TimingLog & log = qar_ar_core::TimingLog::instance();
    if (log.enabled()) {
      std::string stem = qar_ar_core::TimingLog::sanitize_name(key_);
      if (stem.rfind("video_", 0) != 0) {
        stem = "video_" + stem;  // an AR video panel's key is already "video:<topic>"
      }
      timing_sink_ = log.open(
        stem,
        std::vector<std::string>(std::begin(kTimingColumns), std::end(kTimingColumns)),
        std::vector<std::string>(std::begin(kOutcomeWords), std::end(kOutcomeWords)),
        kTimingOutcomeColumn);
    }
  }
  return timing_sink_.get();
}

void
QtPanelStream::clear_pending_resize()
{
  resize_pending_ = false;
  pending_buffer_width_ = 0;
  pending_buffer_height_ = 0;
  pending_ticks_ = 0;
  diagnostics_.resize_pending = false;
  diagnostics_.pending_buffer_width = 0;
  diagnostics_.pending_buffer_height = 0;
}

bool
QtPanelStream::adopt_geometry(const PanelGeometry & desired, double now_seconds)
{
  // Nothing to protect: the first geometry of all, or a stream whose adapter
  // creation failed and is being retried. Delaying either would simply mean the
  // panel never starts.
  if (!adapter_) {
    clear_pending_resize();
    return true;
  }

  // Edge, not level: the widget asking for the same size again this tick is not
  // a new resize, and counting it as one would turn a widget parked a few pixels
  // off the committed size into millions of "ignored resizes" over a session.
  const bool newly_asked = desired.buffer_width != last_desired_buffer_width_ ||
    desired.buffer_height != last_desired_buffer_height_;
  last_desired_buffer_width_ = desired.buffer_width;
  last_desired_buffer_height_ = desired.buffer_height;

  const std::uint32_t delta_w = desired.buffer_width > adapter_buffer_width_
    ? desired.buffer_width - adapter_buffer_width_
    : adapter_buffer_width_ - desired.buffer_width;
  const std::uint32_t delta_h = desired.buffer_height > adapter_buffer_height_
    ? desired.buffer_height - adapter_buffer_height_
    : adapter_buffer_height_ - desired.buffer_height;

  if (delta_w == 0 && delta_h == 0) {
    clear_pending_resize();
    return true;
  }

  if (delta_w < options_.resize_threshold_px && delta_h < options_.resize_threshold_px) {
    // Under the noise floor. Keep streaming at the sender's layout: the widget
    // renders a handful of pixels clipped or with a sliver of background, which
    // nobody can see, against a rebuild everybody can.
    if (newly_asked) {
      ++diagnostics_.resizes_ignored_small;
    }
    clear_pending_resize();
    return false;
  }

  // Significant. Now it has to prove it is also the size the widget is going to
  // KEEP: a different candidate restarts the clock, the same one accumulates.
  if (!resize_pending_ || desired.buffer_width != pending_buffer_width_ ||
    desired.buffer_height != pending_buffer_height_)
  {
    resize_pending_ = true;
    pending_buffer_width_ = desired.buffer_width;
    pending_buffer_height_ = desired.buffer_height;
    pending_since_seconds_ = now_seconds;
    pending_ticks_ = 1;
  } else {
    ++pending_ticks_;
  }
  diagnostics_.resize_pending = true;
  diagnostics_.pending_buffer_width = pending_buffer_width_;
  diagnostics_.pending_buffer_height = pending_buffer_height_;

  // Both gates, deliberately: the seconds are what a 0.25 s self-resize loop or
  // a dragged splitter cannot get past, the ticks are what stops a single stray
  // sample from a panel that ticks slowly counting as "held".
  const bool held_long_enough =
    now_seconds - pending_since_seconds_ >= options_.resize_settle_seconds;
  const bool held_enough_ticks = pending_ticks_ >= options_.resize_settle_ticks;
  if (!held_long_enough || !held_enough_ticks) {
    return false;
  }

  clear_pending_resize();
  return true;
}

bool
QtPanelStream::refresh_geometry(double now_seconds)
{
  if (widget_.isNull()) {
    record_error("no widget");
    return false;
  }

  const PanelGeometryInput input = geometry_input_for(*widget_, options_);

  PanelGeometry geometry;
  std::string error;
  if (!compute_panel_geometry(input, geometry, error)) {
    record_error(std::move(error));
    return false;
  }

  if (!adopt_geometry(geometry, now_seconds)) {
    // Not adopted (yet). Everything below — the capture size, the placement, the
    // logical size the AR hit test normalises against — must stay on the size
    // the LIVE sender was built for, or we would capture one layout and submit
    // it into another. The committed values are already in place from the tick
    // that adopted them, so there is nothing to do but keep them.
    return geometry_.buffer_width > 0 && geometry_.buffer_height > 0;
  }

  logical_width_ = input.logical_width;
  logical_height_ = input.logical_height;
  geometry_ = geometry;

  // geometry.content_scale is exactly what is SENT to QarOS as
  // QarGuiPanelSize::content_scale - always 1.0 (see panel_geometry.cpp). The
  // supersample ratio is not lost: it already did its job on buffer_width/
  // buffer_height above, and geometry.effective_pixel_ratio carries it on for
  // diagnostics via diagnostics_.device_pixel_ratio below.
  placement_.size.width_meters = geometry.width_meters;
  placement_.size.height_meters = geometry.height_meters;
  placement_.size.content_scale = geometry.content_scale;

  diagnostics_.buffer_width = geometry.buffer_width;
  diagnostics_.buffer_height = geometry.buffer_height;
  diagnostics_.width_meters = geometry.width_meters;
  diagnostics_.height_meters = geometry.height_meters;
  diagnostics_.content_scale = geometry.content_scale;
  diagnostics_.device_pixel_ratio = geometry.effective_pixel_ratio;
  // One line per panel, passed to the diagnostics sink by QtPanelStreamManager::tick
  // (which diffs adapter_rebuilds and calls emit_diagnostic exactly like it
  // already does for last_error): logical size, the display's own ratio, the
  // readability supersample on top of it, the effective ratio the two combine
  // to, the content_scale actually SENT (always 1.0 - see above), content and
  // buffer pixel size, and the physical size, which ends with its parts: the video that QarOS
  // draws, the four insets of the frame it draws it inside and the fit margin (panel_geometry.hpp),
  // and, for the panel of a dialog, which fits its width to the frame, how the width was fitted.
  diagnostics_.last_geometry_summary =
    key_ + ": logical " + std::to_string(input.logical_width) + "x" +
    std::to_string(input.logical_height) + " px, dpr " +
    std::to_string(input.device_pixel_ratio) + ", render_scale " +
    std::to_string(input.render_scale) + ", effective_ratio " +
    std::to_string(geometry.effective_pixel_ratio) + ", content_scale sent " +
    std::to_string(geometry.content_scale) + ", content " +
    std::to_string(geometry.content_width) + "x" +
    std::to_string(geometry.content_height) + " px, buffer " +
    std::to_string(geometry.buffer_width) + "x" +
    std::to_string(geometry.buffer_height) + " px, " +
    std::to_string(geometry.width_meters) + "x" +
    std::to_string(geometry.height_meters) + " m" + frame_note(geometry) +
    (options_.dialog_panel ? narrowing_note(geometry) : std::string()) + ")";
  return true;
}

qar_ar_core::PanelPlacement
QtPanelStream::hit_placement() const
{
  // Not the panel that is sent. QarOS draws the video, the whole BUFFER, inside its frame: at the
  // width the left and right insets leave, from the left inset, centred vertically between the
  // top and the bottom inset. The widget fills the top left corner of the buffer (the rest is
  // padding), so the rectangle of the widget is the content's share of the video: a buffer pixel
  // is video width / buffer width wide, the rectangle starts the left inset right of the left
  // edge of the panel, and its top edge is the top inset plus half of what the video leaves of
  // the area under it (the fit margin when the panel has the size its own frame asks for) below
  // the top edge of the panel, where the pose is. What the video does not cover maps onto no
  // pixel. geometry_ is the one the sent size was adopted for.
  qar_ar_core::PanelPlacement placement = placement_;
  const double sent_width = placement_.size.width_meters;
  const double sent_height = placement_.size.height_meters;
  const double video_width = geometry_.video_width_meters;
  if (geometry_.buffer_width == 0 || geometry_.buffer_height == 0 || !(sent_width > 0.0) ||
    !(sent_height > 0.0) || !(video_width > 0.0))
  {
    placement.size.width_meters = 0.0f;
    placement.size.height_meters = 0.0f;
    return placement;
  }
  const double buffer_width = geometry_.buffer_width;
  const double metres_per_buffer_px = video_width / buffer_width;
  const double rect_width = metres_per_buffer_px * geometry_.content_width;
  placement.size.width_meters = static_cast<float>(rect_width);
  placement.size.height_meters =
    static_cast<float>(metres_per_buffer_px * geometry_.content_height);
  const double area_height = sent_height - geometry_.frame.vertical_m();
  const double slack = std::max(0.0, area_height - geometry_.video_height_meters);
  const double left_m = geometry_.frame.left_m;
  const double top_m = geometry_.frame.top_m + 0.5 * slack;
  placement.pose.position = panel_uv_to_room_point(
    placement_, static_cast<float>((left_m + 0.5 * rect_width) / sent_width),
    static_cast<float>(top_m / sent_height));
  return placement;
}

void
QtPanelStream::set_replaced_panel_policy(std::function<bool()> may_keep)
{
  replaced_panel_policy_ = std::move(may_keep);
}

void
QtPanelStream::set_swap_policy(std::function<bool()> swap_allowed)
{
  swap_policy_ = std::move(swap_allowed);
}

std::string
QtPanelStream::common_name_for(std::uint32_t buffer_width, std::uint32_t buffer_height) const
{
  std::string name = qar_ar_core::make_panel_common_name(key_, options_.name_scope);
  if (options_.resize_mode == PanelResizeMode::NewPanel) {
    const std::string size =
      "@" + std::to_string(buffer_width) + "x" + std::to_string(buffer_height);
    if (name.size() + size.size() > qar_ar_core::kCommonNameMax) {
      name.resize(qar_ar_core::kCommonNameMax - size.size());
    }
    name += size;
  }
  return name;
}

bool
QtPanelStream::revive_kept_panel(const qar_ar_core::PanelDesc & desc)
{
  const auto kept = std::find_if(
    replaced_adapters_.begin(), replaced_adapters_.end(), [&desc](const KeptPanel & panel) {
      return !panel.common_name.empty() && panel.common_name == desc.common_name &&
             panel.buffer_width == desc.buffer_width && panel.buffer_height == desc.buffer_height;
    });
  if (kept == replaced_adapters_.end()) {
    return false;
  }
  adapter_ = std::move(kept->adapter);
  const bool was_hidden = kept->hidden;
  replaced_adapters_.erase(kept);
  state_asks_.clear();  // the asks of this sender while it was kept are not ours to undo
  // The kept panel took the pose it had when it was replaced; the panel is shown where the
  // stream is now. A hide that the stream itself asked for stays in force.
  if (!adapter_->set_placement(placement_)) {
    record_error(adapter_->last_error());
  }
  if (was_hidden && !diagnostics_.ar_hidden) {
    if (!adapter_->supports_state() || !adapter_->set_state(QAR_GUI_PANEL_STATE_VISIBLE)) {
      record_visibility_failure(
        adapter_->last_error().empty() ? std::string("the kept panel could not be shown again") :
        adapter_->last_error());
    } else {
      note_state_ask(StateAsk{});
    }
  }
  return true;
}

SessionEndReport
QtPanelStream::end_session()
{
  SessionEndReport report;
  if (injector_) {
    injector_->reset();
    sync_input_diagnostics();
  }
  // A hide that was accepted and then refused must be known before the panels are hidden or
  // left: a flag set when the call was accepted would otherwise leave the panel visible.
  drain_adapter_results(last_tick_seconds_);
  // One panel: hidden where QarOS can and where it is not hidden or closed already, then
  // detached; a sender that cannot detach is closed.
  const auto end_panel = [&report](
    const std::shared_ptr<qar_ar_core::IQarPanelSenderAdapter> & adapter, bool hidden,
    bool closed_in_ar) {
      ++report.panels;
      if (!adapter->supports_detach()) {
        adapter->close(closed_in_ar);
        ++report.closed;
        return;
      }
      if (!hidden && !closed_in_ar) {
        const bool done = adapter->supports_state() &&
          adapter->set_state(QAR_GUI_PANEL_STATE_HIDDEN);
        if (!done) {
          ++report.hide_failures;
          if (report.hide_error.empty()) {
            report.hide_error = adapter->last_error().empty() ?
              std::string("the panel could not be hidden") : adapter->last_error();
          }
        }
      }
      adapter->detach();
      ++report.left_in_place;
    };
  if (adapter_) {
    end_panel(adapter_, diagnostics_.ar_hidden, diagnostics_.ar_panel_closed);
    adapter_.reset();
  }
  for (const KeptPanel & kept : replaced_adapters_) {
    end_panel(kept.adapter, kept.hidden, false);
  }
  replaced_adapters_.clear();
  clear_pending_resize();
  diagnostics_.active = false;
  return report;
}

bool
QtPanelStream::take_size_rebuild(SizeRebuildReport & report)
{
  if (!size_rebuild_pending_) {
    return false;
  }
  size_rebuild_pending_ = false;
  report = size_rebuild_;
  return true;
}

void
QtPanelStream::retire_adapter_for_rebuild(double now_seconds)
{
  SizeRebuildReport report;
  if (options_.resize_mode == PanelResizeMode::Swap && adapter_->supports_detach() &&
    (!swap_policy_ || swap_policy_()))
  {
    // The QarOS panel stays and keeps its common name. Detaching comes FIRST: it releases the old
    // sender and the click/hover subscription of this key, so the sender made next finds the panel
    // with nothing else attached to it, and the old sender is not kept next to the new one (both
    // would point at one QarOS panel).
    adapter_->detach();
    report.fate = ReplacedPanelFate::Swapped;
  } else if (replaced_panel_policy_ && replaced_panel_policy_()) {
    // Hidden and kept, never closed: a close of a QarOS panel while the session runs is what the
    // policy exists to avoid. The hide is asked for here, on the tick that makes the new panel,
    // like every other QarOS call. A refusal changes nothing about keeping it: the old panel
    // then stays visible next to the new one, and the report says so. A refused hide is asked
    // for again on later ticks (retry_kept_hides); a sender that cannot hide a panel at all is
    // not, there is nothing to ask again.
    KeptPanel kept;
    std::string refusal;
    bool refused_by_qaros = false;
    if (!adapter_->supports_state()) {
      refusal = "the panel sender cannot change the state of a panel";
    } else if (!adapter_->set_state(QAR_GUI_PANEL_STATE_HIDDEN)) {
      refused_by_qaros = true;
      refusal = adapter_->last_error().empty() ? std::string("set_state was refused") :
        adapter_->last_error();
    }
    kept.hidden = refusal.empty();
    kept.attempts = 1;
    if (refused_by_qaros) {
      kept.retries_left = kKeptHideRetries;
      kept.next_retry_seconds = now_seconds + kKeptHideRetrySeconds;
    }
    report.fate =
      refusal.empty() ? ReplacedPanelFate::HiddenKept : ReplacedPanelFate::VisibleKept;
    report.hide_error = std::move(refusal);
    kept.common_name = adapter_common_name_;
    kept.buffer_width = adapter_buffer_width_;
    kept.buffer_height = adapter_buffer_height_;
    kept.adapter = std::move(adapter_);
    replaced_adapters_.push_back(std::move(kept));
    state_asks_.clear();
  } else {
    // Fully CLOSED before the replacement is asked for, never overlapped: close() retires the
    // click/hover subscription first (so no callback can arrive naming a panel that is going
    // away), then the QarOS panel, then the video sender.
    adapter_->close();
  }
  adapter_.reset();
  size_rebuild_ = std::move(report);
  size_rebuild_pending_ = true;
}

void
QtPanelStream::retry_kept_hides(double now_seconds)
{
  for (KeptPanel & kept : replaced_adapters_) {
    if (kept.hidden || kept.retries_left == 0 || now_seconds < kept.next_retry_seconds) {
      continue;
    }
    --kept.retries_left;
    kept.next_retry_seconds = now_seconds + kKeptHideRetrySeconds;
    // 2 for the first retry: the attempt that made the rebuild was the first.
    const std::uint32_t attempt = ++kept.attempts;
    const std::string which =
      "'" + key_ + "' the old QarOS panel" +
      (kept.adapter->stream_id_text().empty() ? std::string() :
      " (stream " + kept.adapter->stream_id_text() + ")") +
      " that a size change replaced";
    if (kept.adapter->supports_state() && kept.adapter->set_state(QAR_GUI_PANEL_STATE_HIDDEN)) {
      kept.hidden = true;
      kept.retries_left = 0;
      notes_.push_back(
        which + " is hidden now (attempt " + std::to_string(attempt) + " of " +
        std::to_string(kKeptHideRetries + 1) + "); it is kept until the session ends");
    } else if (kept.retries_left == 0) {
      warnings_.push_back(
        which + " is still visible after " + std::to_string(attempt) +
        " attempts to hide it; it stays visible and is kept until the session ends");
    }
  }
}

bool
QtPanelStream::ensure_adapter(double now_seconds)
{
  // Compare against the size the ADAPTER was built with, not the freshly
  // recomputed geometry: refresh_geometry() has already updated diagnostics_.
  if (adapter_ && adapter_buffer_width_ == geometry_.buffer_width &&
    adapter_buffer_height_ == geometry_.buffer_height)
  {
    return true;
  }

  if (adapter_) {
    // The sender's layout is fixed at creation and QarOS allocates the panel
    // receiver from it, so a resized widget needs a whole new sender + panel.
    retire_adapter_for_rebuild(now_seconds);
  }
  if (!factory_) {
    record_error("no panel sender factory");
    return false;
  }
  if (!adapter_ && has_create_retry_ && now_seconds < create_retry_at_seconds_) {
    return false;  // the last creation failed; asked again once the time has come
  }
  has_create_retry_ = false;

  qar_ar_core::PanelDesc desc;
  desc.key = key_;
  desc.display_name = diagnostics_.display_name;
  desc.buffer_width = geometry_.buffer_width;
  desc.buffer_height = geometry_.buffer_height;
  desc.content_width = geometry_.content_width;
  desc.content_height = geometry_.content_height;
  desc.placement = placement_;
  desc.common_name = common_name_for(geometry_.buffer_width, geometry_.buffer_height);

  // A kept panel with this name is this panel: it is shown again, not made a second time.
  if (!revive_kept_panel(desc)) {
    adapter_ = factory_->create_panel_sender(desc);
  }
  if (!adapter_) {
    record_error(factory_->last_error().empty() ? "panel sender creation failed"
                                                : factory_->last_error());
    diagnostics_.active = false;
    return false;
  }
  adapter_common_name_ = desc.common_name;
  state_asks_.clear();
  adapter_buffer_width_ = geometry_.buffer_width;
  adapter_buffer_height_ = geometry_.buffer_height;
  pose_pending_ = false;  // the sender was built with the current placement
  ++diagnostics_.adapter_rebuilds;
  // A sender, the first or the one a size change needs, shows the widget at a size of its own:
  // the census pair starts again with its first frame.
  census_pair_pending_ = true;
  census_follow_up_armed_ = false;
  diagnostics_.not_ready_streak = 0;
  diagnostics_.not_ready_streak_no_destination = 0;
  diagnostics_.not_ready_streak_no_transfers = 0;
  diagnostics_.not_ready_streak_reconnecting = 0;
  // Asked once, here, rather than on every tick: the id is fixed for the
  // sender's lifetime, and a rebuild mints a new sender and therefore a new id.
  diagnostics_.stream_id = adapter_->stream_id_text();
  // A brand-new sender has served nothing, and the clock on that starts now —
  // not at the panel's creation, so a resize rebuild is judged on its own.
  diagnostics_.unserved_seconds = 0.0;
  diagnostics_.unserved_reported = false;
  unserved_since_seconds_ = 0.0;
  has_unserved_since_ = false;
  // QarOS has seen nothing on this new stream, so the next capture must be sent
  // even if the widget's pixels are byte-identical to the previous sender's.
  capture_.invalidate();
  has_submitted_ = false;
  diagnostics_.active = true;
  diagnostics_.last_error.clear();
  // A hide or show asked for while there was no sender waited for this one.
  apply_visibility_request();
  return true;
}

bool
QtPanelStream::tick(double now_seconds)
{
  // What the senders only queued has an outcome by now, perhaps: it comes before anything acts.
  drain_adapter_results(now_seconds);

  // The kept panels whose hide QarOS refused are asked again when their time has come, before
  // anything can skip this tick: they belong to the stream, not to the panel it shows now.
  retry_kept_hides(now_seconds);

  // The second census of a pair is due on the stream's clock, whatever else this tick does. It
  // describes the panel as the operator sees it, so it waits while the panel is hidden in AR or
  // was closed there and is taken at the first tick after it is on show again: a Warning never
  // names a panel that nobody can see.
  if (census_follow_up_armed_ && now_seconds >= census_follow_up_seconds_ &&
    !diagnostics_.ar_hidden && !diagnostics_.ar_panel_closed)
  {
    census_follow_up_armed_ = false;
    take_census("+1.5 s", true);
  }

  // A pose asked for since the last tick comes first, so a panel that is shown by this tick
  // appears where it is meant to be. Then a hide or show, before anything can skip this tick:
  // showing is also the way back from a close by the operator, which the next check would
  // otherwise end this tick on.
  apply_pending_pose();
  apply_visibility_request();

  // The operator closed this panel in AR. Nothing is looking at the frames any
  // more, so stop paying for them — but the sender stays alive until session
  // shutdown, for the reason on close().
  if (diagnostics_.ar_panel_closed) {
    return false;
  }

  // Every tick feeds the granularity estimate, the ones shed below included.
  tick_interval_.observe(now_seconds);

  // Hidden in AR: nothing is looking at the frames either, and every capture is a full repaint
  // of the widget. The sender, the QarOS panel and the subscriptions stay.
  if (diagnostics_.ar_hidden) {
    ++diagnostics_.frames_skipped_hidden;
    return false;
  }

  // Parked, and not hidden: QarOS has not taken the hide yet, or refused it, and the manager
  // takes the stream over once it sees the refusal. The window is out of use either way (it may
  // be destroyed), so nothing is captured.
  if (diagnostics_.parked) {
    return false;
  }

  // Video arrives on its own clock. Note whether it has since the last capture,
  // so a video panel is captured when its frame lands instead of on a
  // timer-quantised schedule (capture_gate.hpp).
  bool frame_driven = false;
  std::uint64_t frame_seq = 0;
  if (options_.capture_on_frame_arrival && !diagnostics_.tombstoned) {
    frame_seq = frame_sequence(frame_driven);
  }
  diagnostics_.frame_driven = frame_driven;
  if (!frame_driven) {
    has_frame_observation_ = false;
  } else if (!has_frame_observation_ || frame_seq != frame_sequence_observed_) {
    has_frame_observation_ = true;
    frame_sequence_observed_ = frame_seq;
    frame_change_seconds_ = now_seconds;
  }

  // Pace first: everything below (a widget repaint, a full-buffer hash) is real
  // GUI-thread work, and shedding it is the whole point of the cap.
  CaptureGateInput gate;
  gate.now_seconds = now_seconds;
  gate.has_captured = has_ticked_;
  gate.last_capture_seconds = last_tick_seconds_;
  gate.max_fps = options_.max_fps;
  gate.tick_interval_seconds = tick_interval_.interval_seconds();
  gate.frame_driven = frame_driven;
  gate.frame_advanced = frame_driven && frame_seq != frame_sequence_captured_;
  gate.seconds_since_frame = frame_driven ? now_seconds - frame_change_seconds_ : 0.0;
  switch (decide_capture(gate)) {
    case CaptureGateVerdict::kSkipPaced:
      ++diagnostics_.frames_skipped_paced;
      return false;
    case CaptureGateVerdict::kSkipNoNewFrame:
      ++diagnostics_.frames_skipped_no_new_frame;
      return false;
    case CaptureGateVerdict::kCapture:
      break;
  }
  last_tick_seconds_ = now_seconds;
  has_ticked_ = true;
  frame_sequence_captured_ = frame_seq;

  // Per-frame stage timing (QAR_VIDEO_TIMING_DIR; timing_log.hpp), video-driven panels
  // only. Everything below is a null test unless it is on, and when it is on a stamp is one
  // clock read and the row is queued in memory - never written here (this is the GUI
  // thread). The frame's own trace is read NOW, before the capture: the frame it describes
  // was visible before this tick began, so decode->capture can never come out negative; if
  // a newer frame lands during the capture the recorded latency is overstated by at most
  // one frame period, which is the safe direction for a "is it us" measurement.
  TickTiming trace;
  trace.sink = frame_driven ? timing_sink() : nullptr;
  if (trace.sink != nullptr) {
    trace.tick_us = qar_ar_core::timing_epoch_us();
    trace.decode_seq = frame_seq;
    trace.frame = newest_frame_trace();
  }

  // A tombstone's geometry is FROZEN at whatever the live sender was built for.
  // Re-deriving it from the notice widget would be asking a brand-new
  // parentless window for its device pixel ratio, and a ratio that disagreed
  // with the sender's layout by any amount would rebuild the sender — i.e. tear
  // down the very sender the tombstone exists to preserve.
  if (!diagnostics_.tombstoned) {
    if (!refresh_geometry(now_seconds)) {
      return false;
    }
  } else if (geometry_.buffer_width == 0 || geometry_.buffer_height == 0) {
    record_error("tombstoned panel has no committed geometry");
    return false;
  }
  if (!ensure_adapter(now_seconds)) {
    return false;
  }
  // A hide asked for before the sender existed has just been carried out by ensure_adapter().
  if (diagnostics_.ar_hidden) {
    trace.sink = nullptr;  // nothing was captured, so there is no frame to write a row for
    ++diagnostics_.frames_skipped_hidden;
    return false;
  }
  // The sender and its QarOS panel are still being made on the session's thread. Nothing is
  // captured or sent yet, and the wait is not a silence of the receiver: the clock of the
  // unserved panel does not run. The pacing is released so that the first capture follows at once.
  if (adapter_->pending()) {
    trace.sink = nullptr;
    has_ticked_ = false;
    return false;
  }

  // From here a sender exists, so the "we owe QarOS a frame and have not paid"
  // clock is running. It is started here and not at the first not-ready, so it
  // also covers the ticks a capture failure or an unchanged buffer ends early:
  // the headset is waiting either way, and the reason it is waiting is ours to
  // report whatever the local cause turns out to be. `keepalive_seconds`
  // guarantees an unserved panel still ATTEMPTS a submit every tick (the
  // keepalive is unconditionally due while nothing has been submitted), so this
  // never accuses QarOS of a silence we chose.
  if (!has_submitted_) {
    if (!has_unserved_since_) {
      unserved_since_seconds_ = now_seconds;
      has_unserved_since_ = true;
    }
    diagnostics_.unserved_seconds = now_seconds - unserved_since_seconds_;
  }

  if (!capture_.capture(
      geometry_.buffer_width, geometry_.buffer_height, geometry_.effective_pixel_ratio))
  {
    trace.outcome = kOutcomeCaptureFailed;
    record_error(capture_.last_error());
    return false;
  }
  if (trace.sink != nullptr) {
    trace.capture_us = qar_ar_core::timing_epoch_us();
  }
  ++diagnostics_.frames_captured;
  diagnostics_.widget_visible = capture_.widget_visible();
  diagnostics_.last_capture_ms = capture_.last_capture_ms();
  capture_ms_total_ += capture_.last_capture_ms();
  diagnostics_.avg_capture_ms =
    capture_ms_total_ / static_cast<double>(diagnostics_.frames_captured);

  // A burst owed after a long unserved streak counts as a keepalive that is due, so an unchanged
  // frame goes out too (see kBurstStreakTicks).
  const bool keepalive_due = burst_remaining_ > 0 ||
    (options_.keepalive_seconds > 0.0 &&
    (!has_submitted_ || now_seconds - last_submit_seconds_ >= options_.keepalive_seconds));
  if (!capture_.content_changed() && !keepalive_due) {
    trace.outcome = kOutcomeUnchanged;
    ++diagnostics_.frames_skipped_unchanged;
    return false;
  }

  // Only reserve a send cycle once we KNOW we have something to send: a reserved
  // cycle must be followed by a show_frame, so asking earlier would strand it.
  bool ready = false;
  if (!adapter_->try_begin_frame(ready)) {
    record_error(adapter_->last_error());
    return false;
  }
  if (!ready) {
    const qar_ar_core::PanelNotReadyCause cause = adapter_->not_ready_cause();
    if (cause == qar_ar_core::PanelNotReadyCause::Creating) {
      // Not yet, and not a fault: counted nowhere, and the clock of the unserved panel stays off.
      trace.sink = nullptr;
      has_unserved_since_ = false;
      diagnostics_.unserved_seconds = 0.0;
      return false;
    }
    ++diagnostics_.frames_skipped_not_ready;
    // A streak, not just a total: "nothing has been attached to this panel's
    // stream since it was built" and "the receiver blinked once an hour ago"
    // are different situations and the total cannot tell them apart.
    ++diagnostics_.not_ready_streak;
    // ... and by the cause the sender gives: nothing reserved, nothing attached, or a receiver
    // that lost the stream and is reconnecting.
    switch (cause) {
      case qar_ar_core::PanelNotReadyCause::Creating:
        break;  // returned above
      case qar_ar_core::PanelNotReadyCause::NoDestination:
        ++diagnostics_.not_ready_streak_no_destination;
        break;
      case qar_ar_core::PanelNotReadyCause::NoActiveTransfers:
        ++diagnostics_.not_ready_streak_no_transfers;
        break;
      case qar_ar_core::PanelNotReadyCause::Reconnecting:
        ++diagnostics_.not_ready_streak_reconnecting;
        break;
      case qar_ar_core::PanelNotReadyCause::Paused:
        // The receiver paused the stream on purpose (its display is not visible): not a fault, so
        // the wait for a first frame does not run. The tick stays in the streak totals above, which
        // the burst after a long streak reads; the split of the three faulty causes is unchanged.
        has_unserved_since_ = false;
        diagnostics_.unserved_seconds = 0.0;
        break;
    }
    if (trace.sink != nullptr) {
      // The run of ticks in which QarOS offered no destination: this row carries its
      // start, "now" (the latest tick of it so far) and its length.
      trace.outcome = kOutcomeNotReady;
      const std::int64_t now_us = qar_ar_core::timing_epoch_us();
      if (not_ready_run_count_ == 0) {
        not_ready_run_start_us_ = now_us;
      }
      ++not_ready_run_count_;
      trace.not_ready_start_us = not_ready_run_start_us_;
      trace.not_ready_end_us = now_us;
      trace.not_ready_count = not_ready_run_count_;
    }
    return false;
  }
  if (trace.sink != nullptr) {
    trace.ready_us = qar_ar_core::timing_epoch_us();
    if (not_ready_run_count_ > 0) {
      // Closed by this tick: the run lasted from its first tick to the one that found QarOS ready.
      trace.not_ready_start_us = not_ready_run_start_us_;
      trace.not_ready_end_us = trace.ready_us;
      trace.not_ready_count = not_ready_run_count_;
      not_ready_run_count_ = 0;
      not_ready_run_start_us_ = 0;
    }
  }

  if (!adapter_->submit_frame(capture_.pixels())) {
    trace.outcome = kOutcomeSubmitFailed;
    record_error(adapter_->last_error());
    return false;
  }
  if (trace.sink != nullptr) {
    trace.submit_us = qar_ar_core::timing_epoch_us();
    trace.outcome = kOutcomeSubmitted;
  }
  ++diagnostics_.frames_submitted;
  const std::uint64_t unserved_ticks = diagnostics_.not_ready_streak;
  diagnostics_.not_ready_streak = 0;
  diagnostics_.not_ready_streak_no_destination = 0;
  diagnostics_.not_ready_streak_no_transfers = 0;
  diagnostics_.not_ready_streak_reconnecting = 0;
  diagnostics_.unserved_seconds = 0.0;
  diagnostics_.unserved_reported = false;
  has_unserved_since_ = false;
  note_submitted_frame(unserved_ticks, now_seconds);
  if (has_submitted_) {
    // What the viewer experiences as smoothness: the gap between the frames that
    // actually reached QarOS (a fresh sender has no predecessor to measure).
    const double gap_ms = (now_seconds - last_submit_seconds_) * 1000.0;
    if (gap_ms > diagnostics_.submit_gap_max_ms) {
      diagnostics_.submit_gap_max_ms = gap_ms;
    }
    diagnostics_.submit_gap_sum_ms += gap_ms;
    ++diagnostics_.submit_gap_count;
  }
  last_submit_seconds_ = now_seconds;
  has_submitted_ = true;
  diagnostics_.last_error.clear();
  return true;
}

}  // namespace qar_qt_streaming
