// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/panel_pointer_state.hpp"

#include <cmath>
#include <utility>

namespace qar_qt_streaming
{

const char *
to_string(PointerActionKind kind)
{
  switch (kind) {
    case PointerActionKind::Enter:
      return "Enter";
    case PointerActionKind::Move:
      return "Move";
    case PointerActionKind::Press:
      return "Press";
    case PointerActionKind::Drag:
      return "Drag";
    case PointerActionKind::Release:
      return "Release";
    case PointerActionKind::Click:
      return "Click";
    case PointerActionKind::Leave:
      return "Leave";
    case PointerActionKind::Cancel:
      return "Cancel";
  }
  return "Unknown";
}

namespace
{

bool
on_panel(const PointerTarget & target)
{
  return !target.panel_key.empty() && target.inside;
}

}  // namespace

PanelPointerStateMachine::PanelPointerStateMachine(PointerStateConfig config)
: config_(config)
{
}

void
PanelPointerStateMachine::emit(
  std::vector<PointerAction> & out, PointerActionKind kind, std::uint32_t id,
  const std::string & key, double x, double y, bool inside, double t, std::uint32_t click_count,
  qar_ar_core::PointerHand hand)
{
  PointerAction action;
  action.kind = kind;
  action.pointer_id = id;
  action.panel_key = key;
  action.x = x;
  action.y = y;
  action.inside = inside;
  action.timestamp_s = t;
  action.click_count = click_count;
  action.hand = hand;
  out.push_back(std::move(action));
  ++diagnostics_.actions;
}

void
PanelPointerStateMachine::drop(const char * reason)
{
  ++diagnostics_.samples_dropped;
  diagnostics_.last_drop_reason = reason;
}

void
PanelPointerStateMachine::process(
  const ResolvedPointerSample & sample, std::vector<PointerAction> & out)
{
  ++diagnostics_.samples;
  Pointer & p = pointers_[sample.pointer_id];

  switch (sample.gesture) {
    case qar_ar_core::PointerGesture::Hover:
      switch (sample.phase) {
        case qar_ar_core::PointerPhase::Started:
        case qar_ar_core::PointerPhase::Updated:
        case qar_ar_core::PointerPhase::Instant:
          move(p, sample, out);
          break;
        case qar_ar_core::PointerPhase::Ended:
        case qar_ar_core::PointerPhase::Canceled:
          hover_end(p, sample, out);
          break;
      }
      break;
    case qar_ar_core::PointerGesture::Click:
      switch (sample.phase) {
        case qar_ar_core::PointerPhase::Started:
          press(p, sample, out);
          break;
        case qar_ar_core::PointerPhase::Updated:
          move(p, sample, out);
          break;
        case qar_ar_core::PointerPhase::Ended:
          release(p, sample, false, out);
          break;
        case qar_ar_core::PointerPhase::Canceled:
          release(p, sample, true, out);
          break;
        case qar_ar_core::PointerPhase::Instant:
          instant(p, sample, out);
          break;
      }
      break;
    case qar_ar_core::PointerGesture::LongPress:
      // Counted and carried no further, on purpose. See
      // PointerStateDiagnostics::long_presses: the contact that produced it
      // stays live and may still become a drag, which arrives separately as a
      // Click lifecycle, so swallowing this one loses nothing.
      ++diagnostics_.long_presses;
      break;
  }
}

void
PanelPointerStateMachine::set_hover(
  Pointer & p, std::uint32_t id, const std::string & key, double x, double y, double t,
  std::vector<PointerAction> & out)
{
  if (p.hover_key == key) {
    return;
  }
  if (!p.hover_key.empty()) {
    emit(out, PointerActionKind::Leave, id, p.hover_key, p.x, p.y, false, t);
  }
  p.hover_key = key;
  if (!key.empty()) {
    p.x = x;
    p.y = y;
    p.inside = true;
    emit(out, PointerActionKind::Enter, id, key, x, y, true, t);
  }
}

std::uint32_t
PanelPointerStateMachine::detect_double_click(
  Pointer & p, const std::string & key, double x, double y, double t)
{
  const bool is_double = p.has_last_press && p.last_press_key == key &&
    t - p.last_press_time_s <= config_.double_click_interval_s &&
    std::fabs(x - p.last_press_x) <= config_.double_click_distance_px &&
    std::fabs(y - p.last_press_y) <= config_.double_click_distance_px;
  if (is_double) {
    // Qt alternates: the third press in a row is a single again.
    p.has_last_press = false;
    return 2;
  }
  p.has_last_press = true;
  p.last_press_key = key;
  p.last_press_time_s = t;
  p.last_press_x = x;
  p.last_press_y = y;
  return 1;
}

void
PanelPointerStateMachine::abandon_press(
  Pointer & p, std::uint32_t id, double t, std::vector<PointerAction> & out)
{
  if (p.pressed && p.press_on_panel) {
    emit(out, PointerActionKind::Cancel, id, p.grab_key, p.x, p.y, p.inside, t);
  }
  p.pressed = false;
  p.press_on_panel = false;
  p.grab_key.clear();
}

void
PanelPointerStateMachine::move(
  Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out)
{
  const PointerTarget & t = s.target;
  if (p.pressed) {
    if (!p.press_on_panel) {
      drop("move during a press that began off-panel");
      return;
    }
    if (t.panel_key == p.grab_key) {
      p.x = t.x;
      p.y = t.y;
      p.inside = t.inside;
    } else {
      // The resolver could not place the sample on the grabbed panel; the
      // pointer is off it, wherever it is. Keep the last known position.
      p.inside = false;
    }
    emit(out, PointerActionKind::Drag, s.pointer_id, p.grab_key, p.x, p.y, p.inside, s.timestamp_s);
    return;
  }

  const std::string key = on_panel(t) ? t.panel_key : std::string();
  set_hover(p, s.pointer_id, key, t.x, t.y, s.timestamp_s, out);
  if (key.empty()) {
    ++diagnostics_.samples_missed;
    return;
  }
  p.x = t.x;
  p.y = t.y;
  p.inside = true;
  emit(out, PointerActionKind::Move, s.pointer_id, key, t.x, t.y, true, s.timestamp_s);
}

void
PanelPointerStateMachine::hover_end(
  Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out)
{
  if (p.pressed) {
    return;  // the grab holds; the release settles where the pointer ends up
  }
  set_hover(p, s.pointer_id, std::string(), 0.0, 0.0, s.timestamp_s, out);
}

void
PanelPointerStateMachine::press(
  Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out)
{
  // A second STARTED without an end in between: never leave a widget holding
  // a press that will not be released.
  abandon_press(p, s.pointer_id, s.timestamp_s, out);

  const PointerTarget & t = s.target;
  if (!on_panel(t)) {
    set_hover(p, s.pointer_id, std::string(), 0.0, 0.0, s.timestamp_s, out);
    p.pressed = true;
    p.press_on_panel = false;
    ++diagnostics_.samples_missed;
    return;
  }

  set_hover(p, s.pointer_id, t.panel_key, t.x, t.y, s.timestamp_s, out);
  p.x = t.x;
  p.y = t.y;
  p.inside = true;
  p.click_count = click_count_for(p, s, t.panel_key, t.x, t.y);
  p.pressed = true;
  p.press_on_panel = true;
  p.grab_key = t.panel_key;
  emit(
    out, PointerActionKind::Press, s.pointer_id, t.panel_key, t.x, t.y, true, s.timestamp_s,
    p.click_count, s.hand);
}

std::uint32_t
PanelPointerStateMachine::click_count_for(
  Pointer & p, const ResolvedPointerSample & s, const std::string & key, double x, double y)
{
  if (s.tap_count == 0) {
    // No native count: infer from the timestamps, as every source did before
    // the 2026-09-21 SDK and as the app-volume path still does.
    return detect_double_click(p, key, x, y, s.timestamp_s);
  }
  ++diagnostics_.native_tap_counts;
  // The recogniser saw the hand; we only see the pixel it landed on, so its
  // count wins. Clamped to 2 because that is all Qt distinguishes (press,
  // then QEvent::MouseButtonDblClick) and because Qt alternates: a third tap
  // in a run is a single again, which is exactly `1 + (n % 2 == 0)`.
  //
  // The inferred path's memory is cleared so a source that stops reporting tap
  // counts mid-run does not then read this press as the first of a pair.
  p.has_last_press = false;
  return (s.tap_count % 2u) == 0u ? 2u : 1u;
}

void
PanelPointerStateMachine::release(
  Pointer & p, const ResolvedPointerSample & s, bool canceled, std::vector<PointerAction> & out)
{
  if (!p.pressed) {
    drop(canceled ? "cancel without a press" : "release without a press");
    return;
  }
  if (!p.press_on_panel) {
    // The press owned nothing; the pointer is free again and may well be over
    // a panel now.
    p.pressed = false;
    if (!canceled) {
      move(p, s, out);
    }
    return;
  }

  const PointerTarget & t = s.target;
  if (t.panel_key == p.grab_key) {
    p.x = t.x;
    p.y = t.y;
    p.inside = t.inside;
  } else {
    p.inside = false;
  }

  const std::string key = p.grab_key;
  const std::uint32_t clicks = p.click_count;
  p.pressed = false;
  p.press_on_panel = false;
  p.grab_key.clear();

  if (canceled) {
    emit(out, PointerActionKind::Cancel, s.pointer_id, key, p.x, p.y, p.inside, s.timestamp_s);
  } else {
    emit(out, PointerActionKind::Release, s.pointer_id, key, p.x, p.y, p.inside, s.timestamp_s);
    if (p.inside) {
      emit(out, PointerActionKind::Click, s.pointer_id, key, p.x, p.y, true, s.timestamp_s, clicks);
    }
  }

  // Leave was deferred for the whole grab; settle it now.
  if (!p.inside) {
    set_hover(p, s.pointer_id, std::string(), 0.0, 0.0, s.timestamp_s, out);
  }
}

void
PanelPointerStateMachine::instant(
  Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out)
{
  // An INSTANT while a press is pending: the sequence was interrupted, so the
  // pending press is abandoned (never a click), then the tap is delivered.
  abandon_press(p, s.pointer_id, s.timestamp_s, out);

  const PointerTarget & t = s.target;
  if (!on_panel(t)) {
    set_hover(p, s.pointer_id, std::string(), 0.0, 0.0, s.timestamp_s, out);
    ++diagnostics_.samples_missed;
    return;
  }

  set_hover(p, s.pointer_id, t.panel_key, t.x, t.y, s.timestamp_s, out);
  p.x = t.x;
  p.y = t.y;
  p.inside = true;
  const std::uint32_t clicks = click_count_for(p, s, t.panel_key, t.x, t.y);
  emit(
    out, PointerActionKind::Press, s.pointer_id, t.panel_key, t.x, t.y, true, s.timestamp_s,
    clicks, s.hand);
  emit(
    out, PointerActionKind::Release, s.pointer_id, t.panel_key, t.x, t.y, true, s.timestamp_s, 1,
    s.hand);
  emit(
    out, PointerActionKind::Click, s.pointer_id, t.panel_key, t.x, t.y, true, s.timestamp_s,
    clicks, s.hand);
}

bool
PanelPointerStateMachine::is_pressed(std::uint32_t pointer_id) const
{
  const auto it = pointers_.find(pointer_id);
  return it != pointers_.end() && it->second.pressed;
}

std::string
PanelPointerStateMachine::grabbed_panel(std::uint32_t pointer_id) const
{
  const auto it = pointers_.find(pointer_id);
  if (it == pointers_.end() || !it->second.pressed || !it->second.press_on_panel) {
    return std::string();
  }
  return it->second.grab_key;
}

std::string
PanelPointerStateMachine::hovered_panel(std::uint32_t pointer_id) const
{
  const auto it = pointers_.find(pointer_id);
  return it == pointers_.end() ? std::string() : it->second.hover_key;
}

void
PanelPointerStateMachine::forget_panel(
  const std::string & panel_key, std::vector<PointerAction> & out)
{
  for (auto & entry : pointers_) {
    Pointer & p = entry.second;
    const double t = p.last_press_time_s;
    if (p.pressed && p.press_on_panel && p.grab_key == panel_key) {
      abandon_press(p, entry.first, t, out);
    }
    if (p.hover_key == panel_key) {
      set_hover(p, entry.first, std::string(), 0.0, 0.0, t, out);
    }
    if (p.has_last_press && p.last_press_key == panel_key) {
      p.has_last_press = false;  // a re-added panel must not inherit half a double click
    }
  }
}

void
PanelPointerStateMachine::reset(std::vector<PointerAction> & out)
{
  for (auto & entry : pointers_) {
    Pointer & p = entry.second;
    const double t = p.last_press_time_s;
    abandon_press(p, entry.first, t, out);
    set_hover(p, entry.first, std::string(), 0.0, 0.0, t, out);
  }
  pointers_.clear();
}

}  // namespace qar_qt_streaming
