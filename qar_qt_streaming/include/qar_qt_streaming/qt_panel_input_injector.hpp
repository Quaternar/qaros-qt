// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_PANEL_INPUT_INJECTOR_HPP_
#define QAR_QT_STREAMING__QT_PANEL_INPUT_INJECTOR_HPP_

/// @file qt_panel_input_injector.hpp
/// @brief Delivers semantic pointer actions into a live widget tree as real
/// Qt events: the Qt half of turning QarOS GUI-panel clicks into Qt touch and
/// hover events.
///
/// ## What it delivers
///
/// For each @ref PointerAction the injector resolves the deepest visible child
/// under the panel-local point (`QWidget::childAt`, which recurses and honours
/// masks and `WA_TransparentForMouseEvents`), maps the point into that child's
/// frame, and sends:
///
///   * `QEvent::Enter` / `QEvent::Leave` (as `QEnterEvent`) along the widget
///     chain, exactly as Qt's own enter/leave dispatch does — so `underMouse()`
///     and the style's hover state are right — plus `QHoverEvent`s for widgets
///     that opted into `WA_Hover`, and a button-less `QMouseEvent::MouseMove`
///     which Qt turns into `HoverMove`s and tracking moves;
///   * `QTouchEvent` (Begin / Update / End / Cancel) with one touch point per
///     pointer, grouped per receiving widget as Qt groups a touchscreen's
///     points;
///   * `QMouseEvent` press / move / release / double-click, with
///     `Qt::MouseEventSynthesizedByApplication` as the source.
///
/// ## Delivery modes, and why touch-with-mouse-fallback is the default
///
/// Standard Qt controls — QPushButton, QSlider, QLineEdit, QTabBar — do NOT
/// accept touch events (`WA_AcceptTouchEvents` is off), so a touch-only
/// pointer would press nothing. Mouse-only works for all of them but starves
/// the widgets that DO speak touch (kinetic-scrolling views, custom gesture
/// widgets) of the multi-point information an AR pointer actually has. The
/// default therefore does what Qt's own platform layer does for a real
/// touchscreen: offer the sequence as touch first, and if no widget in the
/// chain accepts the `TouchBegin`, synthesize mouse events from that pointer
/// for the rest of its sequence. Only ONE pointer at a time synthesizes mouse
/// (a desktop has one mouse); further simultaneous pointers reach touch-aware
/// widgets only, and are counted as dropped otherwise.
///
/// ## Why events are sent directly, not through the window system
///
/// Qt could be fed through `QWindowSystemInterface` as if a real touchscreen
/// were attached, and it would then do the routing itself. It would also
/// share `qt_button_down`, `QGuiApplication::mouseButtons()` and the popup
/// grab with the operator's REAL mouse on the desktop: an AR press held while
/// the operator drags in the 3D view would corrupt both. The AR pointer is a
/// second, independent input stream, so this injector keeps its own implicit
/// grab, its own enter/leave state and its own focus handling, and sends the
/// events straight to the widgets. The cost is re-implementing the few things
/// `QApplication` only does for spontaneous events — click-to-focus, enter /
/// leave, popup routing — which is what most of this file is.
///
/// ## Thread and lifetime rules
///
/// Every call must be on the Qt GUI thread (the thread that owns the panel
/// widget); calls from any other thread are refused and counted, never
/// executed. The panel and every widget the injector remembers are held
/// through `QPointer`, so a widget destroyed mid-drag drops the action
/// instead of crashing.

#include "qar_qt_streaming/panel_pointer_state.hpp"

#include <QEvent>
#include <QPointF>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class QTouchDevice;

namespace qar_qt_streaming
{

/// @brief How pointer actions become Qt events.
enum class InputDeliveryMode : std::uint8_t
{
  /// Every pointer that can own the mouse synthesizes QMouseEvents; no touch.
  MouseOnly,
  /// QTouchEvents only. Widgets without WA_AcceptTouchEvents get nothing.
  TouchOnly,
  /// Touch first; a sequence nobody accepts falls back to mouse synthesis.
  /// The default — see the file comment.
  TouchWithMouseFallback
};

const char * to_string(InputDeliveryMode mode);

struct PanelInputOptions
{
  /// False => the panel streams out but takes no input.
  bool enabled = true;

  InputDeliveryMode delivery_mode = InputDeliveryMode::TouchWithMouseFallback;

  /// Enter/Leave/Hover and button-less moves. Off for a display-only panel
  /// whose widgets repaint on hover for no benefit.
  bool deliver_hover = true;

  /// Give focus on press to the first widget up the chain whose focus policy
  /// includes Qt::ClickFocus — what QApplication does for a real press, and
  /// what makes a clicked QLineEdit take typing.
  bool apply_focus_policy = true;

  /// While a popup (a combo box list, a menu) is open, route the pointer to
  /// it — Qt sends every mouse event to the active popup, and a press outside
  /// it is how it closes.
  bool route_to_popups = true;

  /// Reproduce Qt's modality rule: while an application-modal window owned by
  /// this panel is up, pointers outside it reach nothing.
  ///
  /// This is the ONLY way a dialog affects its owner panel's input. A dialog
  /// owned by this panel is not drawn into this panel (it is streamed as a panel
  /// of its own — see qt_widget_capture.hpp "Dialogs"), so routing a pointer
  /// that lands on this panel into that dialog would click a widget the operator
  /// cannot see here. Blocking is the whole of the interaction; the dialog takes
  /// its own AR input through its own panel's injector, where `panel_` IS the
  /// dialog and the ordinary resolve path applies.
  ///
  /// Qt enforces this in its platform layer, for SPONTANEOUS events only — and
  /// this injector deliberately bypasses that layer (see "Why events are sent
  /// directly"), so without this a modal dialog would block the desktop mouse
  /// and not the AR one. A hover is turned into a Leave rather than a drop, so
  /// nothing behind the dialog is left looking hovered; presses and releases are
  /// dropped and counted; Leave and Cancel always get through, so a pointer that
  /// was already pressed when the dialog opened is let go rather than stuck.
  bool block_behind_modal = true;
};

struct InputInjectionStats
{
  /// Qt events actually sent into the widget tree.
  std::uint64_t delivered = 0;
  /// Actions that produced no event, with the most recent reason.
  std::uint64_t dropped = 0;
  std::string last_drop_reason;
};

class QtPanelInputInjector
{
public:
  /// @param panel_widget The streamed widget: the root of the tree events are
  ///        delivered into, and the frame `PointerAction::x/y` are in. Borrowed
  ///        through a QPointer; the injector survives its destruction.
  explicit QtPanelInputInjector(QWidget * panel_widget, PanelInputOptions options = {});
  ~QtPanelInputInjector();

  QtPanelInputInjector(const QtPanelInputInjector &) = delete;
  QtPanelInputInjector & operator=(const QtPanelInputInjector &) = delete;

  /// @brief Turn one action into Qt events.
  /// @return true if at least one event was delivered; false (and a counted
  ///         drop with a reason) otherwise.
  bool deliver(const PointerAction & action);

  /// @brief Let go of everything: cancel pressed pointers, leave hovered
  /// widgets, forget all pointers. Safe off-thread only in that it refuses.
  void reset();

  const InputInjectionStats & stats() const { return stats_; }
  const PanelInputOptions & options() const { return options_; }
  QWidget * panel_widget() const { return panel_.data(); }

  /// @brief Widget holding this pointer's implicit grab (press → release).
  QWidget * grab_widget(std::uint32_t pointer_id) const;

  /// @brief Deepest widget this pointer last hovered.
  QWidget * hover_widget(std::uint32_t pointer_id) const;

  /// @brief Which pointer currently synthesizes mouse events, if any.
  bool mouse_pointer(std::uint32_t & out_pointer_id) const;

private:
  enum class Route : std::uint8_t
  {
    None,
    Mouse,
    Touch
  };

  struct Pointer
  {
    QPointer<QWidget> hover;
    QPointer<QWidget> grab;
    Route route = Route::None;
    bool pressed = false;
    QPointF panel_pos;
    QPointF press_pos;
    QPointF last_pos;
    std::uint32_t click_count = 1;
  };

  struct Resolved
  {
    QWidget * target = nullptr;
    QPointF local;
  };

  bool guard(const char *& out_reason) const;
  bool drop(const char * reason);

  Resolved resolve(const QPointF & panel_pos) const;

  QPointF map_from_panel(const QWidget * target, const QPointF & panel_pos) const;
  QPointF window_pos(const QWidget * target, const QPointF & local) const;
  QPointF screen_pos(const QWidget * target, const QPointF & local) const;

  void set_hover_target(Pointer & p, QWidget * target, const QPointF & panel_pos, unsigned long ts);
  void dispatch_enter_leave(QWidget * leave_from, QWidget * enter_to, const QPointF & panel_pos);
  std::vector<QWidget *> chain(QWidget * widget) const;
  unsigned & entered_count(QWidget * widget);

  bool send_mouse(
    QWidget * target, QEvent::Type type, const QPointF & local, Qt::MouseButton button,
    Qt::MouseButtons buttons, unsigned long ts);
  bool send_touch(
    Pointer & p, std::uint32_t pointer_id, QWidget * target, QEvent::Type type,
    Qt::TouchPointState state, unsigned long ts, QWidget *& out_accepting);
  void apply_focus(QWidget * target, const QPointF & local) const;

  bool on_hover_move(std::uint32_t id, Pointer & p, const PointerAction & action);
  bool on_leave(std::uint32_t id, Pointer & p, const PointerAction & action);
  bool on_press(std::uint32_t id, Pointer & p, const PointerAction & action);
  bool on_drag(std::uint32_t id, Pointer & p, const PointerAction & action);
  bool on_release(std::uint32_t id, Pointer & p, const PointerAction & action, bool cancel);

  void release_mouse_ownership(std::uint32_t id);

  QPointer<QWidget> panel_;
  PanelInputOptions options_;
  std::unique_ptr<QTouchDevice> touch_device_;
  std::map<std::uint32_t, Pointer> pointers_;
  bool has_mouse_pointer_ = false;
  std::uint32_t mouse_pointer_ = 0;
  /// Per-widget count of pointers that have entered it: Enter goes out on the
  /// first, Leave on the last, so two hands over one button are one hover.
  std::vector<std::pair<QPointer<QWidget>, unsigned>> entered_;
  InputInjectionStats stats_;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_PANEL_INPUT_INJECTOR_HPP_
