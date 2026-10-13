// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qt_panel_stream.hpp"

#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QWidget>

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

}  // namespace

void
apply_readability(const ReadabilityRecommendation & recommendation, PanelStreamOptions & options)
{
  options.meters_per_logical_px = recommendation.meters_per_logical_px;
  options.render_scale = recommendation.render_scale;
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
  if (options_.input.enabled && widget_ != nullptr) {
    injector_ = std::make_unique<QtPanelInputInjector>(widget_, options_.input);
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
  capture_.set_widget(widget_);

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

bool
QtPanelStream::revive(QWidget * widget)
{
  if (!diagnostics_.tombstoned || widget == nullptr) {
    return false;
  }
  widget_ = widget;
  capture_.set_widget(widget_);
  tombstone_widget_.reset();
  diagnostics_.tombstoned = false;
  diagnostics_.tombstone_reason.clear();
  diagnostics_.tombstone_clipped = false;
  diagnostics_.ar_panel_closed = false;
  if (options_.input.enabled) {
    injector_ = std::make_unique<QtPanelInputInjector>(widget_, options_.input);
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
  diagnostics_.input_events_dropped = stats.dropped;
  diagnostics_.last_input_drop_reason = stats.last_drop_reason;
}

bool
QtPanelStream::deliver_input(const PointerAction & action)
{
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
  if (const auto * video = dynamic_cast<const VideoPanelWidget *>(widget_)) {
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
  if (const auto * video = dynamic_cast<const VideoPanelWidget *>(widget_)) {
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
  if (widget_ == nullptr) {
    record_error("no widget");
    return false;
  }

  PanelGeometryInput input;
  input.logical_width = static_cast<std::uint32_t>(widget_->width() > 0 ? widget_->width() : 0);
  input.logical_height = static_cast<std::uint32_t>(widget_->height() > 0 ? widget_->height() : 0);
  input.device_pixel_ratio = widget_->devicePixelRatioF();
  input.meters_per_logical_px = options_.meters_per_logical_px;
  input.render_scale = options_.render_scale;
  input.buffer_alignment = options_.buffer_alignment;
  input.max_buffer_dimension = options_.max_buffer_dimension;

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
  // buffer pixel size, and the physical size.
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
    std::to_string(geometry.height_meters) + " m";
  return true;
}

bool
QtPanelStream::ensure_adapter()
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
    //
    // Fully CLOSED and released before the replacement is asked for, never
    // overlapped: close() retires the click/hover subscription first (so no
    // callback can arrive naming a panel that is going away), then the QarOS
    // panel, then the video sender — and the runtime must not be holding two
    // senders for one panel while it decides which one owns the destination.
    adapter_->close();
    adapter_.reset();
  }
  if (!factory_) {
    record_error("no panel sender factory");
    return false;
  }

  qar_ar_core::PanelDesc desc;
  desc.key = key_;
  desc.display_name = diagnostics_.display_name;
  desc.buffer_width = geometry_.buffer_width;
  desc.buffer_height = geometry_.buffer_height;
  desc.content_width = geometry_.content_width;
  desc.content_height = geometry_.content_height;
  desc.placement = placement_;

  adapter_ = factory_->create_panel_sender(desc);
  if (!adapter_) {
    record_error(factory_->last_error().empty() ? "panel sender creation failed"
                                                : factory_->last_error());
    diagnostics_.active = false;
    return false;
  }
  adapter_buffer_width_ = geometry_.buffer_width;
  adapter_buffer_height_ = geometry_.buffer_height;
  ++diagnostics_.adapter_rebuilds;
  diagnostics_.not_ready_streak = 0;
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
  return true;
}

bool
QtPanelStream::tick(double now_seconds)
{
  // The operator closed this panel in AR. Nothing is looking at the frames any
  // more, so stop paying for them — but the sender stays alive until session
  // shutdown, for the reason on close().
  if (diagnostics_.ar_panel_closed) {
    return false;
  }

  // Every tick feeds the granularity estimate, the ones shed below included.
  tick_interval_.observe(now_seconds);

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
  if (!ensure_adapter()) {
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

  const bool keepalive_due = options_.keepalive_seconds > 0.0 &&
    (!has_submitted_ || now_seconds - last_submit_seconds_ >= options_.keepalive_seconds);
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
    ++diagnostics_.frames_skipped_not_ready;
    // A streak, not just a total: "nothing has been attached to this panel's
    // stream since it was built" and "the receiver blinked once an hour ago"
    // are different situations and the total cannot tell them apart.
    ++diagnostics_.not_ready_streak;
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
  diagnostics_.not_ready_streak = 0;
  diagnostics_.unserved_seconds = 0.0;
  diagnostics_.unserved_reported = false;
  has_unserved_since_ = false;
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
