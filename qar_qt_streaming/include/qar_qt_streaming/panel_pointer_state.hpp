// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_POINTER_STATE_HPP_
#define QAR_QT_STREAMING__PANEL_POINTER_STATE_HPP_

/// @file panel_pointer_state.hpp
/// @brief Turns raw gesture samples into semantic pointer actions. Pure,
/// Qt-free, so every transition is unit-tested without a QApplication.
///
/// The runtime says "click STARTED at this point"; a widget needs "pointer 3
/// PRESSED panel X at (x, y), and is still the one you owe a release to, even
/// though it has since wandered off the panel". This state machine is the gap
/// between the two. It knows nothing about geometry (the manager resolves
/// samples to a panel and a widget pixel before they arrive here, see
/// @ref ResolvedPointerSample) and nothing about Qt (the injector turns the
/// actions into events). What it owns is the SEMANTICS:
///
///   * **Hover** — Enter / Move / Leave as a free pointer crosses panels.
///   * **Press / Drag / Release / Click** — a click that reports STARTED /
///     UPDATED / ENDED phases, or an INSTANT one that collapses all three.
///     Since the 2026-09-21 SDK the per-panel path produces BOTH: a
///     `QAR_GUI_PANEL_GESTURE_KIND_DRAG` lifecycle is the phased form and a
///     `..._TAP` is the instant one, so a QSlider handle can be dragged in AR
///     through the same transitions.
///   * **Grab** — from Press to Release every sample of that pointer is routed
///     to the pressed panel, whatever it is over now, with coordinates that
///     may lie outside the panel. This is Qt's implicit mouse grab, and it is
///     what makes "press a button, slide off, release" NOT a click.
///   * **Cancel** — a CANCELED phase, a panel that disappears mid-press, or a
///     source that goes away: the pressed widget is told to let go without
///     committing anything.
///   * **Multiple pointers** — every pointer id has its own state; two hands
///     can hover two panels or press the same one.
///
/// Phases follow `QarGesturePhase` exactly (see panel_input_source.hpp), so
/// the app-volume API and the per-panel API feed the same transitions.

#include "qar_ar_core/panel_input_source.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace qar_qt_streaming
{

/// @brief Where geometry placed a sample.
struct PointerTarget
{
  /// Panel the coordinates refer to; empty when the sample landed on none.
  std::string panel_key;
  /// Widget LOGICAL pixels. May lie outside [0, w) × [0, h) while the pointer
  /// is grabbed and has been dragged past the panel's edge.
  double x = 0.0;
  double y = 0.0;
  bool inside = false;
};

/// @brief A sample after the manager resolved it against the live panels.
struct ResolvedPointerSample
{
  std::uint32_t pointer_id = 0;
  qar_ar_core::PointerGesture gesture = qar_ar_core::PointerGesture::Hover;
  qar_ar_core::PointerPhase phase = qar_ar_core::PointerPhase::Updated;
  double timestamp_s = 0.0;
  /// Carried through from the seam (PanelPointerSample::hand) so a consumer can
  /// tell two hands apart; the machine itself keys on pointer_id.
  qar_ar_core::PointerHand hand = qar_ar_core::PointerHand::Unknown;
  /// Native tap count, 0 when the source does not count taps. See
  /// PanelPointerSample::tap_count.
  std::uint8_t tap_count = 0;
  PointerTarget target;
};

/// @brief What a pointer did, in terms a widget understands.
enum class PointerActionKind : std::uint8_t
{
  Enter,    ///< The pointer arrived over the panel.
  Move,     ///< Moved while not pressed (a hover move).
  Press,    ///< Pressed on the panel; the panel now owns the pointer.
  Drag,     ///< Moved while pressed; routed to the pressed panel, may be outside it.
  Release,  ///< Released; `inside` says whether it was still over the panel.
  Click,    ///< Follows a Release that happened inside the pressed panel.
  Leave,    ///< The pointer left the panel (never sent while pressed).
  Cancel    ///< The press was abandoned; let go without committing.
};

const char * to_string(PointerActionKind kind);

struct PointerAction
{
  PointerActionKind kind = PointerActionKind::Move;
  std::uint32_t pointer_id = 0;
  std::string panel_key;
  double x = 0.0;
  double y = 0.0;
  bool inside = true;
  double timestamp_s = 0.0;
  /// For Press and Click: 2 on the second press of a double click, else 1.
  /// Taken from the recogniser's own tap count when the source reports one
  /// (`qar_gui_panel_gesture_get_tap_count`), and inferred from the timestamps
  /// otherwise — see @ref PointerStateDiagnostics::native_tap_counts.
  std::uint32_t click_count = 1;
  /// Which hand produced it, when known. Purely informational downstream: Qt
  /// has one mouse, so the injector ignores it and a diagnostics panel does not.
  qar_ar_core::PointerHand hand = qar_ar_core::PointerHand::Unknown;
};

/// @brief Tunables. Defaults are Qt's own desktop defaults, so a pure test
/// and the Qt injector agree on what a double click is.
struct PointerStateConfig
{
  /// QApplication::doubleClickInterval() default.
  double double_click_interval_s = 0.4;
  /// QStyleHints::mouseDoubleClickDistance() default, in logical px per axis.
  double double_click_distance_px = 5.0;
};

struct PointerStateDiagnostics
{
  std::uint64_t samples = 0;
  /// Samples that resolved to no panel while nothing was pressed: the wearer is
  /// pointing at nothing. Expected traffic, but worth seeing in a diagnostics
  /// panel when every click seems to vanish.
  std::uint64_t samples_missed = 0;
  /// Samples that made no sense in the pointer's state (a release without a
  /// press, a move during a press that began off-panel).
  std::uint64_t samples_dropped = 0;
  std::string last_drop_reason;
  std::uint64_t actions = 0;
  /// Presses whose click_count came from the recogniser rather than from the
  /// timestamp heuristic. Non-zero means the native path is live.
  std::uint64_t native_tap_counts = 0;
  /// LONG_PRESS samples seen. Carried through the seam and counted here, but
  /// deliberately producing no action: Qt has no long-press event, and
  /// inventing one (a synthetic right-click, say) is a product decision nobody
  /// has made. Wired so that adding a consumer is a change here alone.
  std::uint64_t long_presses = 0;
};

class PanelPointerStateMachine
{
public:
  explicit PanelPointerStateMachine(PointerStateConfig config = PointerStateConfig{});

  /// @brief Feed one sample; the actions it produces are appended to @p out
  /// in delivery order (e.g. Leave-old, Enter-new, Move).
  void process(const ResolvedPointerSample & sample, std::vector<PointerAction> & out);

  bool is_pressed(std::uint32_t pointer_id) const;

  /// @brief Panel a pointer is pressed on — the one the manager must keep
  /// projecting its samples onto. Empty when not pressed on a panel.
  std::string grabbed_panel(std::uint32_t pointer_id) const;

  /// @brief Panel a pointer is currently over, or empty.
  std::string hovered_panel(std::uint32_t pointer_id) const;

  /// @brief Pointers seen so far (idle ones included: they keep the
  /// double-click memory).
  std::size_t pointer_count() const { return pointers_.size(); }

  /// @brief A panel went away: Cancel for pointers pressed on it, Leave for
  /// pointers over it. Called before the panel's widget is torn down.
  void forget_panel(const std::string & panel_key, std::vector<PointerAction> & out);

  /// @brief Drop every pointer (the source disconnected): Cancel + Leave as
  /// appropriate, then forget them all.
  void reset(std::vector<PointerAction> & out);

  const PointerStateDiagnostics & diagnostics() const { return diagnostics_; }

private:
  struct Pointer
  {
    std::string hover_key;
    bool pressed = false;
    /// False for a press that began off every panel: it owns nothing, and its
    /// moves are ignored until it ends (Qt gives a widget nothing for a drag
    /// that started elsewhere).
    bool press_on_panel = false;
    std::string grab_key;
    double x = 0.0;
    double y = 0.0;
    bool inside = false;
    std::uint32_t click_count = 1;

    bool has_last_press = false;
    std::string last_press_key;
    double last_press_time_s = 0.0;
    double last_press_x = 0.0;
    double last_press_y = 0.0;
  };

  void move(Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out);
  void hover_end(Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out);
  void press(Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out);
  void release(
    Pointer & p, const ResolvedPointerSample & s, bool canceled, std::vector<PointerAction> & out);
  void instant(Pointer & p, const ResolvedPointerSample & s, std::vector<PointerAction> & out);

  /// Sync hover_key to @p key, emitting Leave/Enter as needed.
  void set_hover(
    Pointer & p, std::uint32_t id, const std::string & key, double x, double y, double t,
    std::vector<PointerAction> & out);
  std::uint32_t detect_double_click(Pointer & p, const std::string & key, double x, double y, double t);
  /// Native tap count when the source reports one, the timestamp heuristic
  /// otherwise. One place, so Press and Instant cannot drift apart.
  std::uint32_t click_count_for(
    Pointer & p, const ResolvedPointerSample & s, const std::string & key, double x, double y);
  void abandon_press(Pointer & p, std::uint32_t id, double t, std::vector<PointerAction> & out);
  void drop(const char * reason);

  void emit(
    std::vector<PointerAction> & out, PointerActionKind kind, std::uint32_t id,
    const std::string & key, double x, double y, bool inside, double t,
    std::uint32_t click_count = 1, qar_ar_core::PointerHand hand = qar_ar_core::PointerHand::Unknown);

  PointerStateConfig config_;
  std::map<std::uint32_t, Pointer> pointers_;
  PointerStateDiagnostics diagnostics_;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_POINTER_STATE_HPP_
