// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/seam/panel_gesture_mapper.hpp"

namespace qar_qt_streaming::seam
{

std::uint32_t
PanelGestureMapper::pointer_id_for(const PeerId & peer, PointerHand hand)
{
  // Two hands of one peer are two pointers, and the hands of two peers are four, each with its own
  // grab and its own double-click memory. Without the hand every contact in the room would share
  // one pointer and a second operator could steal the press of a first one.
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < sizeof(peer.data); ++i) {
    hash ^= static_cast<unsigned char>(peer.data[i]);
    hash *= 16777619u;
  }
  hash ^= static_cast<unsigned char>(hand);
  hash *= 16777619u;
  // Clear of 0 (the manager's implicit room pointer) and 1 (a pointer shared by all contacts).
  return (hash % 0xFFFFF000u) + 16u;
}

bool
PanelGestureMapper::uv_from_content_pixels(
  std::int32_t x_pixels, std::int32_t y_pixels, std::uint32_t width, std::uint32_t height,
  float & out_u, float & out_v, bool & out_clamped)
{
  if (width == 0 || height == 0) {
    return false;
  }
  // The runtime documents the pixel as clamped to the content already, and signed for headroom.
  // A panel that was resized under a subscription made for its old size still delivers
  // coordinates of the old size: a stale one must land on the edge of the panel, and a negative
  // one on the near edge instead of wrapping through an unsigned conversion.
  const std::int32_t last_x = static_cast<std::int32_t>(width) - 1;
  const std::int32_t last_y = static_cast<std::int32_t>(height) - 1;
  out_clamped = x_pixels < 0 || y_pixels < 0 || x_pixels > last_x || y_pixels > last_y;
  const std::int32_t x = x_pixels < 0 ? 0 : (x_pixels > last_x ? last_x : x_pixels);
  const std::int32_t y = y_pixels < 0 ? 0 : (y_pixels > last_y ? last_y : y_pixels);

  // +0.5 addresses the centre of the pixel; the content space and the widget space share the
  // origin (top-left) and the direction of both axes, so no axis is flipped. The content scale of
  // the panel is not applied: the runtime maps the contact through it before it publishes.
  out_u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
  out_v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
  return true;
}

bool
PanelGestureMapper::pointer_gesture_from_kind(PanelGestureKind kind, PointerGesture & out)
{
  switch (kind) {
    case PanelGestureKind::Hover:
      out = PointerGesture::Hover;
      return true;
    case PanelGestureKind::Tap:
    case PanelGestureKind::Drag:
      // Both are a press. A tap arrives Instant and the state machine expands it into press,
      // release and click; a drag spells the same three out over its lifecycle.
      out = PointerGesture::Click;
      return true;
    case PanelGestureKind::LongPress:
      out = PointerGesture::LongPress;
      return true;
    case PanelGestureKind::Unknown:
      break;
  }
  return false;
}

bool
PanelGestureMapper::pointer_phase_from_phase(PanelGesturePhase phase, PointerPhase & out)
{
  switch (phase) {
    case PanelGesturePhase::Started:
      out = PointerPhase::Started;
      return true;
    case PanelGesturePhase::Updated:
      out = PointerPhase::Updated;
      return true;
    case PanelGesturePhase::Ended:
      out = PointerPhase::Ended;
      return true;
    case PanelGesturePhase::Instant:
      out = PointerPhase::Instant;
      return true;
    case PanelGesturePhase::Canceled:
      out = PointerPhase::Canceled;
      return true;
    case PanelGesturePhase::Unknown:
      break;
  }
  return false;
}

GestureOutcome
PanelGestureMapper::offer(
  const PanelGestureEvent & event, const GestureSlot & slot, double now_s,
  PanelPointerSample & sample)
{
  ++stats_.events_received;

  if (!event.readable) {
    // There is no "invalid" flag: an accessor that refuses IS the invalid event, and skipping
    // means skipping, not a leave and not a guess.
    ++stats_.events_invalid;
    return GestureOutcome::Invalid;
  }

  PointerGesture pointer_gesture = PointerGesture::Hover;
  PointerPhase pointer_phase = PointerPhase::Updated;
  if (!pointer_gesture_from_kind(event.kind, pointer_gesture) ||
    !pointer_phase_from_phase(event.phase, pointer_phase))
  {
    // A kind or a phase this build does not know: counted apart, so that it reads as a newer
    // runtime and not as a broken one.
    ++stats_.events_unknown_kind;
    return GestureOutcome::UnknownKind;
  }

  switch (event.kind) {
    case PanelGestureKind::Drag:
      ++stats_.events_drag;
      break;
    case PanelGestureKind::LongPress:
      ++stats_.events_long_press;
      break;
    case PanelGestureKind::Tap:
      if (event.tap_count >= 2) {
        ++stats_.events_multi_tap;
      }
      break;
    case PanelGestureKind::Hover:
    case PanelGestureKind::Unknown:
      break;
  }

  float u = 0.0f;
  float v = 0.0f;
  bool clamped = false;
  if (!uv_from_content_pixels(
      event.x_pixels, event.y_pixels, slot.content_width, slot.content_height, u, v, clamped))
  {
    return GestureOutcome::NoContentSize;
  }
  if (clamped) {
    ++stats_.events_clamped;
  }

  sample = PanelPointerSample();
  sample.pointer_id = pointer_id_for(event.peer, event.hand);
  sample.gesture = pointer_gesture;
  sample.phase = pointer_phase;
  sample.hand = event.hand;
  // The recogniser's own count. It is 0 for every kind but TAP, and 0 is exactly what tells the
  // state machine to infer a double click from the timestamps.
  sample.tap_count = event.tap_count;
  sample.timestamp_s = now_s;
  sample.has_panel_uv = true;
  sample.panel_key = slot.panel_key;
  sample.u = u;
  sample.v = v;

  // Only the phased kinds have a lifecycle to keep alive; a tap and a long press are Instant and
  // are gone the moment they land. A gesture id is unique only within a source peer and is
  // shared by the kinds of that peer, so the key holds the kind as well.
  if (pointer_phase != PointerPhase::Instant) {
    LifecycleKey key;
    key.peer.assign(reinterpret_cast<const char *>(event.peer.data), sizeof(event.peer.data));
    key.gesture_id = event.gesture_id;
    key.kind = static_cast<int>(event.kind);
    if (pointer_phase == PointerPhase::Ended || pointer_phase == PointerPhase::Canceled) {
      live_.erase(key);
    } else {
      LiveLifecycle & entry = live_[key];
      entry.pointer_id = sample.pointer_id;
      entry.panel_key = slot.panel_key;
      entry.gesture = pointer_gesture;
      entry.hand = event.hand;
      entry.u = u;
      entry.v = v;
      entry.last_seen_s = now_s;
    }
  }
  return GestureOutcome::Sampled;
}

PanelPointerSample
PanelGestureMapper::cancel_sample(const LiveLifecycle & live, double now_s)
{
  PanelPointerSample cancel;
  cancel.pointer_id = live.pointer_id;
  cancel.gesture = live.gesture;
  cancel.phase = PointerPhase::Canceled;
  cancel.hand = live.hand;
  cancel.timestamp_s = now_s;
  // The last place the gesture was seen, so that the cancel lands on the grabbed panel and not on
  // nothing. A hover cancel becomes a leave in the pointer state machine.
  cancel.has_panel_uv = true;
  cancel.panel_key = live.panel_key;
  cancel.u = live.u;
  cancel.v = live.v;
  return cancel;
}

void
PanelGestureMapper::sweep_lifecycles(double now_s, std::vector<PanelPointerSample> & out)
{
  if (lifecycle_timeout_s_ <= 0.0) {
    return;
  }
  for (auto it = live_.begin(); it != live_.end();) {
    if (now_s - it->second.last_seen_s < lifecycle_timeout_s_) {
      ++it;
      continue;
    }
    // Three heartbeats of silence: the producer is gone, not idle. A drag that ends here must not
    // leave the widget holding the press, and a hover must not leave a button lit.
    ++stats_.lifecycle_timeouts;
    if (it->second.gesture == PointerGesture::Hover) {
      ++stats_.hover_timeouts;
    }
    out.push_back(cancel_sample(it->second, now_s));
    it = live_.erase(it);
  }
}

void
PanelGestureMapper::cancel_lifecycles_for_panel(
  const std::string & panel_key, double now_s, std::vector<PanelPointerSample> & out)
{
  for (auto it = live_.begin(); it != live_.end();) {
    if (!panel_key.empty() && it->second.panel_key != panel_key) {
      ++it;
      continue;
    }
    out.push_back(cancel_sample(it->second, now_s));
    it = live_.erase(it);
  }
}

}  // namespace qar_qt_streaming::seam
