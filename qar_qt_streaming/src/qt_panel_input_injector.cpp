// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qt_panel_input_injector.hpp"

#include "qar_qt_streaming/offscreen_popup_guard.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QThread>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace qar_qt_streaming
{

const char *
to_string(InputDeliveryMode mode)
{
  switch (mode) {
    case InputDeliveryMode::MouseOnly:
      return "MouseOnly";
    case InputDeliveryMode::TouchOnly:
      return "TouchOnly";
    case InputDeliveryMode::TouchWithMouseFallback:
      return "TouchWithMouseFallback";
  }
  return "Unknown";
}

namespace
{

unsigned long
to_event_ms(double seconds)
{
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return 0;
  }
  return static_cast<unsigned long>(std::llround(seconds * 1000.0));
}

bool
contains(const std::vector<QWidget *> & widgets, const QWidget * widget)
{
  return std::find(widgets.begin(), widgets.end(), widget) != widgets.end();
}

/// QApplicationPrivate::shouldSetFocus, on public API.
bool
should_set_focus(QWidget * widget, Qt::FocusPolicy policy)
{
  QWidget * proxy = widget;
  while (proxy->focusProxy() != nullptr) {
    proxy = proxy->focusProxy();
  }
  if ((widget->focusPolicy() & policy) != policy) {
    return false;
  }
  if (widget != proxy && (proxy->focusPolicy() & policy) != policy) {
    return false;
  }
  return true;
}

/// The popup @p w lives in when that popup is kept off the desktop (popup-class top level
/// with WA_DontShowOnScreen), else null. Never the panel: see popup_global_origin().
const QWidget *
kept_off_popup_of(const QWidget * w)
{
  const QWidget * top = w != nullptr ? w->window() : nullptr;
  if (top == nullptr || !top->testAttribute(Qt::WA_DontShowOnScreen)) {
    return nullptr;
  }
  const Qt::WindowType type = top->windowType();
  return (type == Qt::Popup || type == Qt::ToolTip) ? top : nullptr;
}

QPoint
global_to_widget(const QWidget * target, const QPoint & global)
{
  if (const QWidget * popup = kept_off_popup_of(target)) {
    const QPoint in_popup = global - popup_global_origin(popup);
    return target == popup ? in_popup : target->mapFrom(popup, in_popup);
  }
  return target->mapFromGlobal(global);
}

QPoint
widget_to_global(const QWidget * target, const QPoint & local)
{
  if (const QWidget * popup = kept_off_popup_of(target)) {
    return popup_global_origin(popup) + (target == popup ? local : target->mapTo(popup, local));
  }
  return target->mapToGlobal(local);
}

}  // namespace

QtPanelInputInjector::QtPanelInputInjector(QWidget * panel_widget, PanelInputOptions options)
: panel_(panel_widget),
  options_(std::move(options)),
  touch_device_(std::make_unique<QTouchDevice>())
{
  // Not registered with QWindowSystemInterface on purpose: registration is
  // for devices the platform delivers spontaneous events from, and this one
  // never does. Widgets only ever inspect its type and capabilities.
  touch_device_->setName(QStringLiteral("QarOS panel pointer"));
  touch_device_->setType(QTouchDevice::TouchScreen);
  touch_device_->setCapabilities(QTouchDevice::Position);
  touch_device_->setMaximumTouchPoints(10);
  // Make this panel known to dialog attribution before it can take a single
  // action: a registered panel is one whose OWN dialogs can never be claimed by
  // another panel's click. See PanelOverlayAttribution.
  PanelOverlayAttribution::instance().register_panel(panel_.data());
}

QtPanelInputInjector::~QtPanelInputInjector()
{
  // Best effort only: off-thread or after the panel is gone there is nothing
  // safe to send, and reset() refuses on its own.
  reset();
  PanelOverlayAttribution::instance().unregister_panel(panel_.data());
}

bool
QtPanelInputInjector::guard(const char *& out_reason) const
{
  if (QCoreApplication::instance() == nullptr) {
    out_reason = "no QApplication";
    return false;
  }
  if (panel_.isNull()) {
    out_reason = "panel widget destroyed";
    return false;
  }
  if (QThread::currentThread() != panel_->thread()) {
    out_reason = "not on the Qt GUI thread that owns the panel widget";
    return false;
  }
  return true;
}

bool
QtPanelInputInjector::drop(const char * reason)
{
  ++stats_.dropped;
  stats_.last_drop_reason = reason;
  return false;
}

QWidget *
QtPanelInputInjector::grab_widget(std::uint32_t pointer_id) const
{
  const auto it = pointers_.find(pointer_id);
  return it == pointers_.end() ? nullptr : it->second.grab.data();
}

QWidget *
QtPanelInputInjector::hover_widget(std::uint32_t pointer_id) const
{
  const auto it = pointers_.find(pointer_id);
  return it == pointers_.end() ? nullptr : it->second.hover.data();
}

bool
QtPanelInputInjector::mouse_pointer(std::uint32_t & out_pointer_id) const
{
  if (!has_mouse_pointer_) {
    return false;
  }
  out_pointer_id = mouse_pointer_;
  return true;
}

// --- coordinates -------------------------------------------------------------

QPointF
QtPanelInputInjector::map_from_panel(const QWidget * target, const QPointF & panel_pos) const
{
  if (target == panel_.data()) {
    return panel_pos;
  }
  // QWidget's mapping is integer; carry the sub-pixel part across unchanged so
  // a slow AR drag does not quantise to whole pixels.
  const QPoint rounded = panel_pos.toPoint();
  const QPointF fraction = panel_pos - QPointF(rounded);
  const QPoint mapped = panel_->isAncestorOf(target) ?
    target->mapFrom(panel_.data(), rounded) :
    global_to_widget(target, panel_->mapToGlobal(rounded));
  return QPointF(mapped) + fraction;
}

QPointF
QtPanelInputInjector::window_pos(const QWidget * target, const QPointF & local) const
{
  const QPoint rounded = local.toPoint();
  return QPointF(target->mapTo(target->window(), rounded)) + (local - QPointF(rounded));
}

QPointF
QtPanelInputInjector::screen_pos(const QWidget * target, const QPointF & local) const
{
  const QPoint rounded = local.toPoint();
  return QPointF(widget_to_global(target, rounded)) + (local - QPointF(rounded));
}

QtPanelInputInjector::Resolved
QtPanelInputInjector::resolve(const QPointF & panel_pos) const
{
  Resolved resolved;
  if (options_.route_to_popups) {
    if (QWidget * popup = QApplication::activePopupWidget()) {
      // Qt sends EVERY mouse event to the active popup while one is open. Inside
      // it the deepest child gets it; outside, the popup itself does — which is
      // how a combo box list closes on a press beside it.
      const QPoint global = panel_->mapToGlobal(panel_pos.toPoint());
      const QPoint in_popup = global_to_widget(popup, global);
      QWidget * target = popup->rect().contains(in_popup) ? popup->childAt(in_popup) : nullptr;
      if (target == nullptr) {
        target = popup;
      }
      resolved.target = target;
      resolved.local = map_from_panel(target, panel_pos);
      return resolved;
    }
  }

  // No dialog branch: a dialog owned by this panel is NOT drawn into this
  // panel's image, so a point on this panel can only ever mean a widget of this
  // panel. The dialog answers AR input through the panel of its own that
  // QtPanelStreamManager gives it — and while it is application-modal,
  // block_behind_modal above refuses this panel's input entirely.
  const QPoint rounded = panel_pos.toPoint();
  // childAt recurses to the deepest visible child, honouring masks and
  // WA_TransparentForMouseEvents — the same resolution a real mouse gets.
  QWidget * target = panel_->rect().contains(rounded) ? panel_->childAt(rounded) : nullptr;
  if (target == nullptr) {
    target = panel_.data();
  }
  resolved.target = target;
  resolved.local = map_from_panel(target, panel_pos);
  return resolved;
}

// --- enter / leave -----------------------------------------------------------

std::vector<QWidget *>
QtPanelInputInjector::chain(QWidget * widget) const
{
  std::vector<QWidget *> out;
  for (QWidget * w = widget; w != nullptr; w = w->parentWidget()) {
    out.push_back(w);
    if (w == panel_.data() || w->isWindow()) {
      break;
    }
  }
  return out;
}

unsigned &
QtPanelInputInjector::entered_count(QWidget * widget)
{
  // Dead entries drop out as they are met; the list is a handful of widgets.
  entered_.erase(
    std::remove_if(
      entered_.begin(), entered_.end(),
      [](const std::pair<QPointer<QWidget>, unsigned> & e) { return e.first.isNull(); }),
    entered_.end());
  for (auto & entry : entered_) {
    if (entry.first.data() == widget) {
      return entry.second;
    }
  }
  entered_.emplace_back(QPointer<QWidget>(widget), 0u);
  return entered_.back().second;
}

void
QtPanelInputInjector::dispatch_enter_leave(
  QWidget * leave_from, QWidget * enter_to, const QPointF & panel_pos)
{
  const std::vector<QWidget *> leaving = chain(leave_from);
  const std::vector<QWidget *> entering = chain(enter_to);

  // Leave innermost first, as QApplicationPrivate::dispatchEnterLeave does.
  for (QWidget * w : leaving) {
    if (contains(entering, w)) {
      continue;
    }
    unsigned & count = entered_count(w);
    if (count > 0) {
      --count;
    }
    if (count != 0) {
      continue;  // another pointer is still over it
    }
    const QPointF local = map_from_panel(w, panel_pos);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(w, &leave);
    ++stats_.delivered;
    if (w->testAttribute(Qt::WA_Hover)) {
      QHoverEvent hover(QEvent::HoverLeave, QPointF(-1.0, -1.0), local);
      QCoreApplication::sendEvent(w, &hover);
      ++stats_.delivered;
    }
  }

  // Enter outermost first.
  for (auto it = entering.rbegin(); it != entering.rend(); ++it) {
    QWidget * w = *it;
    if (contains(leaving, w)) {
      continue;
    }
    unsigned & count = entered_count(w);
    ++count;
    if (count != 1) {
      continue;
    }
    const QPointF local = map_from_panel(w, panel_pos);
    QEnterEvent enter(local, window_pos(w, local), screen_pos(w, local));
    QCoreApplication::sendEvent(w, &enter);
    ++stats_.delivered;
    if (w->testAttribute(Qt::WA_Hover)) {
      QHoverEvent hover(QEvent::HoverEnter, local, QPointF(-1.0, -1.0));
      QCoreApplication::sendEvent(w, &hover);
      ++stats_.delivered;
    }
  }
}

void
QtPanelInputInjector::set_hover_target(
  Pointer & p, QWidget * target, const QPointF & panel_pos, unsigned long ts)
{
  (void)ts;
  if (p.hover.data() == target) {
    return;
  }
  dispatch_enter_leave(p.hover.data(), target, panel_pos);
  p.hover = target;
}

// --- event synthesis ---------------------------------------------------------

bool
QtPanelInputInjector::send_mouse(
  QWidget * target, QEvent::Type type, const QPointF & local, Qt::MouseButton button,
  Qt::MouseButtons buttons, unsigned long ts)
{
  QMouseEvent event(
    type, local, window_pos(target, local), screen_pos(target, local), button, buttons,
    Qt::NoModifier, Qt::MouseEventSynthesizedByApplication);
  event.setTimestamp(ts);
  QCoreApplication::sendEvent(target, &event);
  ++stats_.delivered;
  return true;
}

bool
QtPanelInputInjector::send_touch(
  Pointer & p, std::uint32_t pointer_id, QWidget * target, QEvent::Type type,
  Qt::TouchPointState state, unsigned long ts, QWidget *& out_accepting)
{
  out_accepting = nullptr;

  // Qt groups a touchscreen's points per receiving widget: every pointer this
  // widget currently holds rides along, stationary, so the widget sees the
  // whole gesture and not one finger at a time.
  QList<QTouchEvent::TouchPoint> points;
  Qt::TouchPointStates states;
  const auto add_point = [&](std::uint32_t id, const Pointer & q, Qt::TouchPointState s) {
    QTouchEvent::TouchPoint point(static_cast<int>(id));
    point.setState(Qt::TouchPointStates(s));
    const QPointF local = map_from_panel(target, q.panel_pos);
    const QPointF start = map_from_panel(target, q.press_pos);
    const QPointF last = map_from_panel(target, q.last_pos);
    point.setPos(local);
    point.setStartPos(start);
    point.setLastPos(last);
    // For widgets the scene position IS the screen position (QTouchEvent docs).
    point.setScenePos(screen_pos(target, local));
    point.setStartScenePos(screen_pos(target, start));
    point.setLastScenePos(screen_pos(target, last));
    point.setScreenPos(screen_pos(target, local));
    point.setStartScreenPos(screen_pos(target, start));
    point.setLastScreenPos(screen_pos(target, last));
    point.setPressure(s == Qt::TouchPointReleased ? 0.0 : 1.0);
    points.append(point);
    states |= s;
  };

  if (type != QEvent::TouchCancel) {
    add_point(pointer_id, p, state);
    for (const auto & entry : pointers_) {
      const Pointer & q = entry.second;
      if (entry.first == pointer_id || !q.pressed || q.route != Route::Touch ||
        q.grab.data() != target)
      {
        continue;
      }
      add_point(entry.first, q, Qt::TouchPointStationary);
    }
  }

  QTouchEvent event(type, touch_device_.get(), Qt::NoModifier, states, points);
  event.setWindow(target->window()->windowHandle());
  event.setTarget(target);
  event.setTimestamp(ts);
  QCoreApplication::sendEvent(target, &event);
  ++stats_.delivered;

  if (event.isAccepted()) {
    // QApplication::notify walks a TouchBegin up the parent chain and leaves
    // target() on the widget that took it — that widget owns the sequence.
    QWidget * accepting = qobject_cast<QWidget *>(event.target());
    out_accepting = accepting != nullptr ? accepting : target;
  }
  return true;
}

void
QtPanelInputInjector::apply_focus(QWidget * target, const QPointF & local) const
{
  // QApplicationPrivate::giveFocusAccordingToFocusPolicy, which QApplication
  // runs only for SPONTANEOUS presses: walk up from the pressed widget to the
  // first one that takes click focus, stopping at one that already has focus.
  QWidget * widget = target;
  QPointF pos = local;
  while (widget != nullptr) {
    if (widget->isEnabled() && widget->rect().contains(pos.toPoint()) &&
      should_set_focus(widget, Qt::ClickFocus))
    {
      widget->setFocus(Qt::MouseFocusReason);
      return;
    }
    if (widget->isWindow()) {
      return;
    }
    QWidget * focused = widget->focusProxy() != nullptr ? widget->focusProxy() : widget;
    if (focused->hasFocus()) {
      return;
    }
    pos += QPointF(widget->pos());
    widget = widget->parentWidget();
  }
}

void
QtPanelInputInjector::release_mouse_ownership(std::uint32_t id)
{
  if (has_mouse_pointer_ && mouse_pointer_ == id) {
    has_mouse_pointer_ = false;
  }
}

// --- actions -----------------------------------------------------------------

bool
QtPanelInputInjector::deliver(const PointerAction & action)
{
  const char * reason = nullptr;
  if (!guard(reason)) {
    return drop(reason);
  }
  if (!options_.enabled) {
    return drop("input is disabled for this panel");
  }

  Pointer & p = pointers_[action.pointer_id];

  // Modality, which Qt only enforces for the spontaneous events this injector
  // bypasses. Blocking is WHOLESALE: there is no "but the pointer is over the
  // dialog" exemption, because an owned dialog is not drawn into this panel at
  // all — it is streamed as a panel of its own. Every point on
  // this panel therefore shows a widget BEHIND the modal window, and Qt gives a
  // real mouse nothing there either. The dialog answers through its own panel's
  // injector, for which the dialog IS the panel and this rule does not fire.
  if (options_.block_behind_modal && panel_owned_modal_window(panel_.data()) != nullptr) {
    switch (action.kind) {
      case PointerActionKind::Enter:
      case PointerActionKind::Move:
        // Not a drop: the widget the pointer WAS over must stop looking
        // hovered, and a Leave is exactly how Qt says so.
        p.last_pos = p.panel_pos;
        p.panel_pos = QPointF(action.x, action.y);
        return on_leave(action.pointer_id, p, action);
      case PointerActionKind::Leave:
      case PointerActionKind::Cancel:
        break;  // cleanup always gets through
      default:
        return drop("blocked by a modal dialog owned by this panel");
    }
  }

  // Causation, recorded BEFORE the action is delivered — delivering it is what
  // opens the dialog, and QDialog::exec() does not return until the operator
  // dismisses it. Only ACTIVATING actions count: a hover opens nothing, so
  // letting one count would hand a dialog to whichever panel the pointer
  // happened to drift over between the press and the first capture tick.
  switch (action.kind) {
    case PointerActionKind::Press:
    case PointerActionKind::Release:
    case PointerActionKind::Click:
      PanelOverlayAttribution::instance().note_panel_input(panel_.data());
      break;
    default:
      break;
  }

  switch (action.kind) {
    case PointerActionKind::Enter:
    case PointerActionKind::Move:
      return on_hover_move(action.pointer_id, p, action);
    case PointerActionKind::Leave:
      return on_leave(action.pointer_id, p, action);
    case PointerActionKind::Press:
      return on_press(action.pointer_id, p, action);
    case PointerActionKind::Drag:
      return on_drag(action.pointer_id, p, action);
    case PointerActionKind::Release:
      return on_release(action.pointer_id, p, action, false);
    case PointerActionKind::Cancel:
      return on_release(action.pointer_id, p, action, true);
    case PointerActionKind::Click:
      // Qt derives clicks from press + release itself (QAbstractButton::
      // hitButton on release); there is no event to send.
      return true;
  }
  return drop("unknown pointer action");
}

bool
QtPanelInputInjector::on_hover_move(std::uint32_t id, Pointer & p, const PointerAction & action)
{
  p.last_pos = p.panel_pos;
  p.panel_pos = QPointF(action.x, action.y);
  if (p.pressed) {
    return on_drag(id, p, action);  // the state machine never does this; be safe
  }
  if (!options_.deliver_hover) {
    return true;
  }
  const unsigned long ts = to_event_ms(action.timestamp_s);
  const Resolved resolved = resolve(p.panel_pos);
  set_hover_target(p, resolved.target, p.panel_pos, ts);
  // A button-less move: QApplication hands it to widgets with mouse tracking
  // and turns it into HoverMove for WA_Hover ones (sliders, tabs, buttons).
  return send_mouse(
    resolved.target, QEvent::MouseMove, resolved.local, Qt::NoButton, Qt::NoButton, ts);
}

bool
QtPanelInputInjector::on_leave(std::uint32_t id, Pointer & p, const PointerAction & action)
{
  (void)id;
  const unsigned long ts = to_event_ms(action.timestamp_s);
  set_hover_target(p, nullptr, p.panel_pos, ts);
  return true;
}

bool
QtPanelInputInjector::on_press(std::uint32_t id, Pointer & p, const PointerAction & action)
{
  if (p.pressed) {
    on_release(id, p, action, true);  // never two presses without a release
  }
  const unsigned long ts = to_event_ms(action.timestamp_s);
  p.panel_pos = QPointF(action.x, action.y);
  p.press_pos = p.panel_pos;
  p.last_pos = p.panel_pos;
  p.click_count = action.click_count;

  const Resolved resolved = resolve(p.panel_pos);
  // A real pointer enters before it presses.
  set_hover_target(p, resolved.target, p.panel_pos, ts);

  QWidget * grab = nullptr;
  Route route = Route::None;
  const char * reason = nullptr;

  if (options_.delivery_mode != InputDeliveryMode::MouseOnly) {
    // A second finger landing inside (or around) a widget that already holds a
    // touch sequence joins that sequence, as Qt's closest-target rule does.
    QWidget * touch_target = resolved.target;
    bool joins = false;
    for (const auto & entry : pointers_) {
      const Pointer & q = entry.second;
      if (entry.first == id || !q.pressed || q.route != Route::Touch || q.grab.isNull()) {
        continue;
      }
      if (q.grab.data() == touch_target || q.grab->isAncestorOf(touch_target) ||
        touch_target->isAncestorOf(q.grab.data()))
      {
        touch_target = q.grab.data();
        joins = true;
        break;
      }
    }
    const QEvent::Type type = joins ? QEvent::TouchUpdate : QEvent::TouchBegin;

    p.pressed = true;  // so the group includes this pointer
    p.route = Route::Touch;
    p.grab = touch_target;
    QWidget * accepting = nullptr;
    send_touch(p, id, touch_target, type, Qt::TouchPointPressed, ts, accepting);
    if (accepting != nullptr) {
      grab = accepting;
      route = Route::Touch;
    } else {
      p.pressed = false;
      p.route = Route::None;
      p.grab = nullptr;
    }
  }

  if (route == Route::None) {
    if (options_.delivery_mode == InputDeliveryMode::TouchOnly) {
      reason = "no widget under the pointer accepts touch events (touch-only mode)";
    } else if (!has_mouse_pointer_ || mouse_pointer_ == id) {
      has_mouse_pointer_ = true;
      mouse_pointer_ = id;
      route = Route::Mouse;
      grab = resolved.target;
    } else {
      reason = "a second pointer pressed while another one owns the synthesized mouse";
    }
  }

  p.pressed = true;
  p.route = route;
  p.grab = grab;
  if (route == Route::None) {
    return drop(reason);
  }

  if (options_.apply_focus_policy) {
    apply_focus(resolved.target, resolved.local);
  }
  if (route == Route::Mouse) {
    const QPointF local = map_from_panel(grab, p.panel_pos);
    send_mouse(grab, QEvent::MouseButtonPress, local, Qt::LeftButton, Qt::LeftButton, ts);
    if (p.click_count >= 2) {
      // Qt's platform layer sends the press AND a double-click for the second
      // press of a pair; QWidget's default handler turns the latter into
      // another press, which is what most widgets expect.
      send_mouse(grab, QEvent::MouseButtonDblClick, local, Qt::LeftButton, Qt::LeftButton, ts);
    }
  }
  return true;
}

bool
QtPanelInputInjector::on_drag(std::uint32_t id, Pointer & p, const PointerAction & action)
{
  p.last_pos = p.panel_pos;
  p.panel_pos = QPointF(action.x, action.y);
  if (!p.pressed) {
    return on_hover_move(id, p, action);
  }
  if (p.route == Route::None) {
    return drop("pointer owns no input route (its press was dropped)");
  }
  QWidget * grab = p.grab.data();
  if (grab == nullptr) {
    p.pressed = false;
    p.route = Route::None;
    release_mouse_ownership(id);
    return drop("grabbed widget destroyed during a drag");
  }
  const unsigned long ts = to_event_ms(action.timestamp_s);
  if (p.route == Route::Mouse) {
    QWidget * target = grab;
    if (options_.route_to_popups) {
      if (QWidget * popup = QApplication::activePopupWidget()) {
        if (popup != grab && !popup->isAncestorOf(grab)) {
          target = resolve(p.panel_pos).target;  // Qt: popups take everything
        }
      }
    }
    return send_mouse(
      target, QEvent::MouseMove, map_from_panel(target, p.panel_pos), Qt::NoButton,
      Qt::LeftButton, ts);
  }
  QWidget * accepting = nullptr;
  return send_touch(p, id, grab, QEvent::TouchUpdate, Qt::TouchPointMoved, ts, accepting);
}

bool
QtPanelInputInjector::on_release(
  std::uint32_t id, Pointer & p, const PointerAction & action, bool cancel)
{
  p.last_pos = p.panel_pos;
  p.panel_pos = QPointF(action.x, action.y);
  if (!p.pressed) {
    return drop(cancel ? "cancel without a press" : "release without a press");
  }
  const Route route = p.route;
  QWidget * grab = p.grab.data();
  p.pressed = false;
  p.route = Route::None;
  p.grab = nullptr;
  if (route == Route::Mouse) {
    release_mouse_ownership(id);
  }
  if (route == Route::None) {
    return drop("pointer owned no input route (its press was dropped)");
  }
  if (grab == nullptr) {
    return drop("grabbed widget destroyed before its release");
  }

  const unsigned long ts = to_event_ms(action.timestamp_s);
  bool sent = false;
  if (route == Route::Mouse) {
    QWidget * target = grab;
    if (options_.route_to_popups) {
      if (QWidget * popup = QApplication::activePopupWidget()) {
        if (popup != grab && !popup->isAncestorOf(grab)) {
          target = resolve(p.panel_pos).target;
        }
      }
    }
    // A cancel is a release nowhere: (-1, -1) is outside every widget's rect,
    // so QAbstractButton::hitButton() fails and nothing is clicked, while the
    // widget still gets to drop its pressed state.
    const QPointF local = cancel ? QPointF(-1.0, -1.0) : map_from_panel(target, p.panel_pos);
    sent = send_mouse(
      target, QEvent::MouseButtonRelease, local, Qt::LeftButton, Qt::NoButton, ts);
  } else {
    bool others = false;
    for (const auto & entry : pointers_) {
      const Pointer & q = entry.second;
      if (entry.first != id && q.pressed && q.route == Route::Touch && q.grab.data() == grab) {
        others = true;
        break;
      }
    }
    const QEvent::Type type =
      cancel ? QEvent::TouchCancel : (others ? QEvent::TouchUpdate : QEvent::TouchEnd);
    QWidget * accepting = nullptr;
    sent = send_touch(p, id, grab, type, Qt::TouchPointReleased, ts, accepting);
  }

  // The grab held the hover target through the whole press. If the pointer is
  // still over the panel, settle on whatever is under it now; if not, the
  // state machine's Leave follows and clears it.
  if (options_.deliver_hover && action.inside) {
    const Resolved resolved = resolve(p.panel_pos);
    set_hover_target(p, resolved.target, p.panel_pos, ts);
  }
  return sent;
}

void
QtPanelInputInjector::reset()
{
  const char * reason = nullptr;
  if (guard(reason)) {
    for (auto & entry : pointers_) {
      Pointer & p = entry.second;
      if (p.pressed) {
        PointerAction cancel;
        cancel.kind = PointerActionKind::Cancel;
        cancel.pointer_id = entry.first;
        cancel.x = p.panel_pos.x();
        cancel.y = p.panel_pos.y();
        cancel.inside = false;
        on_release(entry.first, p, cancel, true);
      }
      set_hover_target(p, nullptr, p.panel_pos, 0);
    }
    // This panel stops being operable from AR: a list it opened that only exists in its AR
    // image would otherwise hold the application in popup mode with nobody able to reach it.
    // Popups on the desktop (visible panels) are left alone.
    close_offscreen_popups(panel_.data());
  }
  pointers_.clear();
  entered_.clear();
  has_mouse_pointer_ = false;
}

}  // namespace qar_qt_streaming
