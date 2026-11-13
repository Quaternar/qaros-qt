// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qaros/qaros_panel_input_source.hpp"

#include "backend_detail.hpp"
#include "qar_qt_streaming/qaros/conversions.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace qar_qt_streaming::qaros_backend
{

namespace
{

double
steady_seconds()
{
  using namespace std::chrono;
  return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

/// All of the first 20 events, then one in 50: the first ones answer "is anything arriving", the
/// tail shows that a stream is still running and where it points.
bool
should_trace(std::uint64_t index)
{
  return index <= 20 || (index % 50) == 0;
}

/// Upper bound on the trace records that wait for the next drain, so that a manager that stopped
/// ticking cannot turn the trace into an unbounded allocation on the SDK's thread.
constexpr std::size_t kMaxPendingTrace = 256;

const char *
kind_name(seam::PanelGestureKind kind)
{
  switch (kind) {
    case seam::PanelGestureKind::Hover: return "hover";
    case seam::PanelGestureKind::Tap: return "tap";
    case seam::PanelGestureKind::LongPress: return "long-press";
    case seam::PanelGestureKind::Drag: return "drag";
    case seam::PanelGestureKind::Unknown: break;
  }
  return "unknown-kind";
}

const char *
phase_name(seam::PanelGesturePhase phase)
{
  switch (phase) {
    case seam::PanelGesturePhase::Started: return "started";
    case seam::PanelGesturePhase::Updated: return "updated";
    case seam::PanelGesturePhase::Ended: return "ended";
    case seam::PanelGesturePhase::Instant: return "instant";
    case seam::PanelGesturePhase::Canceled: return "canceled";
    case seam::PanelGesturePhase::Unknown: break;
  }
  return "unknown-phase";
}

const char *
skip_reason(seam::GestureOutcome outcome)
{
  switch (outcome) {
    case seam::GestureOutcome::Sampled: return "";
    case seam::GestureOutcome::Invalid: return "the handle could not be read";
    case seam::GestureOutcome::UnknownKind:
      return "gesture kind or phase outside the enums this build knows";
    case seam::GestureOutcome::NoContentSize: return "subscription has no content size";
  }
  return "";
}

seam::PanelGestureKind
kind_from_sdk(qaros::GuiPanelGestureKind kind)
{
  switch (kind) {
    case qaros::GuiPanelGestureKind::Hover: return seam::PanelGestureKind::Hover;
    case qaros::GuiPanelGestureKind::Tap: return seam::PanelGestureKind::Tap;
    case qaros::GuiPanelGestureKind::LongPress: return seam::PanelGestureKind::LongPress;
    case qaros::GuiPanelGestureKind::Drag: return seam::PanelGestureKind::Drag;
  }
  return seam::PanelGestureKind::Unknown;
}

seam::PanelGesturePhase
phase_from_sdk(qaros::GesturePhase phase)
{
  switch (phase) {
    case qaros::GesturePhase::Started: return seam::PanelGesturePhase::Started;
    case qaros::GesturePhase::Updated: return seam::PanelGesturePhase::Updated;
    case qaros::GesturePhase::Ended: return seam::PanelGesturePhase::Ended;
    case qaros::GesturePhase::Instant: return seam::PanelGesturePhase::Instant;
    case qaros::GesturePhase::Canceled: return seam::PanelGesturePhase::Canceled;
  }
  return seam::PanelGesturePhase::Unknown;
}

seam::PointerHand
hand_from_sdk(qaros::Handedness hand)
{
  switch (hand) {
    case qaros::Handedness::Left: return seam::PointerHand::Left;
    case qaros::Handedness::Right: return seam::PointerHand::Right;
    case qaros::Handedness::Unspecified: break;
  }
  return seam::PointerHand::Unknown;
}

}  // namespace

QarosPanelInputSource::QarosPanelInputSource(InputSourceOptions options)
: clock_(options.clock ? std::move(options.clock) : std::function<double()>(&steady_seconds)),
  mapper_(options.lifecycle_timeout_s)
{
}

QarosPanelInputSource::~QarosPanelInputSource()
{
  unsubscribe_all();
}

double
QarosPanelInputSource::now_s() const
{
  return clock_();
}

void
QarosPanelInputSource::set_diagnostics(seam::PanelDiagnosticsSink * sink)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  sink_ = sink;
}

void
QarosPanelInputSource::set_message_flush(std::function<void()> flush)
{
  flush_messages_ = std::move(flush);
}

void
QarosPanelInputSource::set_lifecycle_timeout(double seconds)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  mapper_.set_lifecycle_timeout(seconds);
}

bool
QarosPanelInputSource::subscribe_panel(
  const qaros::GuiPanelControl & panels, const std::string & panel_key,
  const qaros::GuiPanelId & id, std::uint32_t content_width, std::uint32_t content_height,
  std::string & error)
{
  if (panel_key.empty()) {
    error = "panel input needs a panel key to report samples against";
    return false;
  }
  if (content_width == 0 || content_height == 0) {
    error = "panel input needs a non-zero content size to normalise against";
    return false;
  }

  // Subscribing a key again (a widget was resized, so the panel was made anew) ends the old
  // subscription first: two live ones would double every click.
  unsubscribe_panel(panel_key);

  seam::GestureSlot slot;
  slot.panel_key = panel_key;
  slot.content_width = content_width;
  slot.content_height = content_height;

  // One subscription for every kind, not one per kind: a hover that ends and the drag that grows
  // out of the same contact keep the order in which the runtime published them, and the pointer
  // state machine's grab depends on that order. The callback holds the source by a raw pointer;
  // the subscription is ended before the source is destroyed.
  qaros::Result<qaros::Subscription> subscribed = panels.SubscribeAllGestureEvents(
    id, [this, slot](qaros::Result<qaros::GuiPanelGesture> gesture) { on_gesture(slot, gesture); });
  if (!subscribed.Ok()) {
    error = detail::failure_text("SubscribeAllGestureEvents", subscribed.GetStatus());
    return false;
  }
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    panels_[panel_key] = std::move(subscribed).Value();
    // A key that was closed in AR and is subscribed again (its slot was taken over by a new
    // widget) starts its close history afresh.
    closed_.erase(panel_key);
  }
  return true;
}

void
QarosPanelInputSource::unsubscribe_panel(const std::string & panel_key)
{
  qaros::Subscription subscription;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = panels_.find(panel_key);
    if (it == panels_.end()) {
      return;
    }
    subscription = std::move(it->second);
    panels_.erase(it);
  }
  // Ended outside the lock: it waits for a callback that is running, and the callback takes the
  // lock.
  subscription.End();
  // No callback runs for the panel any more. What is still held down on it is let go, queued like
  // any other sample, so that a widget left in the middle of a press gets its release.
  std::vector<seam::PanelPointerSample> cancels;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    mapper_.cancel_lifecycles_for_panel(panel_key, now_s(), cancels);
  }
  for (const seam::PanelPointerSample & cancel : cancels) {
    queue_.push(cancel);
  }
}

void
QarosPanelInputSource::unsubscribe_all()
{
  std::vector<std::string> keys;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : panels_) {
      keys.push_back(entry.first);
    }
  }
  for (const std::string & key : keys) {
    unsubscribe_panel(key);
  }
}

bool
QarosPanelInputSource::has_panel(const std::string & panel_key) const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return panels_.find(panel_key) != panels_.end();
}

void
QarosPanelInputSource::report_closed(const std::string & panel_key)
{
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!closed_.insert(panel_key).second) {
      return;  // already reported; the runtime republishes a closed panel freely
    }
  }
  queue_.push_closed_panel(panel_key);
}

void
QarosPanelInputSource::on_gesture(
  const seam::GestureSlot & slot, const qaros::Result<qaros::GuiPanelGesture> & gesture)
{
  // Library thread. Short, no I/O, no SDK call, no Qt, and nothing here may release the last
  // reference to a session.
  seam::PanelGestureEvent event;
  if (gesture.Ok()) {
    const qaros::GuiPanelGesture & value = gesture.Value();
    event.peer = to_seam(value.SourcePeerId);
    event.gesture_id = value.GestureId;
    event.kind = kind_from_sdk(value.Kind);
    event.phase = phase_from_sdk(value.Phase);
    event.x_pixels = value.Position.XPixels;
    event.y_pixels = value.Position.YPixels;
    event.hand = hand_from_sdk(value.Handedness);
    event.tap_count = value.TapCount;
  } else {
    event.readable = false;
  }

  seam::PanelPointerSample sample;
  bool queue_it = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    const seam::GestureOutcome outcome = mapper_.offer(event, slot, now_s(), sample);
    queue_it = outcome == seam::GestureOutcome::Sampled;
    const std::uint64_t index = mapper_.stats().events_received;
    if (sink_ != nullptr && should_trace(index) && pending_trace_.size() < kMaxPendingTrace) {
      TraceRecord record;
      record.index = index;
      record.panel_key = slot.panel_key;
      record.kind = event.kind;
      record.phase = event.phase;
      record.x_pixels = event.x_pixels;
      record.y_pixels = event.y_pixels;
      record.gesture_id = event.gesture_id;
      record.hand = event.hand;
      record.tap_count = event.tap_count;
      record.outcome = outcome;
      record.u = sample.u;
      record.v = sample.v;
      pending_trace_.push_back(std::move(record));
    }
  }
  if (queue_it) {
    queue_.push(sample);
  }
}

void
QarosPanelInputSource::drain(std::vector<seam::PanelPointerSample> & out)
{
  std::vector<seam::PanelPointerSample> samples;
  queue_.drain(samples);

  std::vector<TraceRecord> trace;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    trace.swap(pending_trace_);
    // The liveness backstop. A hover that ends says so, so this is not how a widget normally gets
    // its leave; it is what happens when the producer stops talking in the middle of a gesture.
    mapper_.sweep_lifecycles(now_s(), samples);
  }
  // Qt thread: the sink may be called here, and the trace is delivered here for that reason.
  if (flush_messages_) {
    flush_messages_();
  }
  if (sink_ != nullptr) {
    for (const TraceRecord & r : trace) {
      char text[256] = {0};
      if (r.outcome == seam::GestureOutcome::Sampled) {
        std::snprintf(
          text, sizeof(text) - 1,
          "#%llu %s/%s id=%llu hand=%s taps=%u x=%d y=%d -> u=%.4f v=%.4f",
          static_cast<unsigned long long>(r.index), kind_name(r.kind), phase_name(r.phase),
          static_cast<unsigned long long>(r.gesture_id), seam::to_string(r.hand),
          static_cast<unsigned>(r.tap_count), r.x_pixels, r.y_pixels,
          static_cast<double>(r.u), static_cast<double>(r.v));
      } else {
        std::snprintf(
          text, sizeof(text) - 1, "#%llu %s/%s id=%llu x=%d y=%d -> SKIPPED (%s)",
          static_cast<unsigned long long>(r.index), kind_name(r.kind), phase_name(r.phase),
          static_cast<unsigned long long>(r.gesture_id), r.x_pixels, r.y_pixels,
          skip_reason(r.outcome));
      }
      sink_->on_message(seam::DiagnosticSeverity::Info, "qar-panel-input", text, r.panel_key);
    }
  }
  for (seam::PanelPointerSample & sample : samples) {
    out.push_back(std::move(sample));
  }
}

void
QarosPanelInputSource::drain_closed_panels(std::vector<std::string> & out)
{
  queue_.drain_closed_panels(out);
}

std::size_t
QarosPanelInputSource::subscription_count() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return panels_.size();
}

seam::GestureStats
QarosPanelInputSource::stats() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return mapper_.stats();
}

}  // namespace qar_qt_streaming::qaros_backend
