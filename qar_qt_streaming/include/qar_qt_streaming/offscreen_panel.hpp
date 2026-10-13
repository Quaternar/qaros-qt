// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__OFFSCREEN_PANEL_HPP_
#define QAR_QT_STREAMING__OFFSCREEN_PANEL_HPP_

/// @file offscreen_panel.hpp
/// @brief A top-level QWidget that is "shown" - it lays out, paints and takes
/// input like any visible window - but never reaches the desktop, plus the pure
/// parser for the size an operator may choose for one.
///
/// ## What this is for
///
/// Streaming a widget into AR needs it to be VISIBLE in Qt's sense:
/// `QWidget::render()` (the whole capture, qt_widget_capture.hpp) paints only
/// children that are `isVisible()`, and the input injector (qt_panel_input_injector.hpp)
/// resolves its targets with `childAt()`, which skips hidden children too. A widget
/// that exists only to be looked at in the headset - a second copy of a dock panel
/// of a host such as an RViz plugin is one example - therefore has to be shown, and
/// showing a top-level widget normally puts a window on the operator's desktop.
///
/// `Qt::WA_DontShowOnScreen` is Qt's answer: the widget is mapped as far as Qt's own
/// bookkeeping goes (`isVisible()` is true, children are laid out, painting and event
/// delivery work, popups it opens position against it) but no platform window is ever
/// made visible. @ref show_offscreen sets that, and the three other things a
/// parentless, never-on-screen window needs to behave:
///
///  * **`Qt::WA_QuitOnClose` off.** A visible, parentless top-level widget with this
///    attribute counts as "a window the application still has open", so with it set
///    this widget would keep the application running after its main window closed -
///    for a long-running host, a process that never exits.
///  * **`Qt::WindowDoesNotAcceptFocus`.** `QWidget::activateWindow()` on it - which
///    a host's own panels may call after a dialog, to force keyboard focus back on
///    the main window - becomes a no-op instead of stealing the foreground from the
///    real main window, and its keyboard shortcuts (a panel that binds Ctrl+N or
///    Ctrl+D on its buttons, say) can never fire, because a shortcut only
///    triggers while its window is the active one.
///  * **A fixed size** (`setFixedSize`). The streamed panel's buffer layout is fixed
///    when its video sender is created, and a size change tears the sender, the QarOS
///    panel and its input subscription down and builds them again
///    (QtPanelStream::refresh_geometry), so a surface that can never change size can
///    never trigger that. Content taller than the surface scrolls inside it.
///
/// and it is moved to the primary screen's top-left corner before it is shown. That
/// is not decoration: Qt places a popup against the screen of the widget that opens it
/// (a completer list or a tooltip is also clamped to that screen), and the capture maps
/// the popup back into the panel through global coordinates; a window parked off every
/// screen would have its popups pulled back onto the screen and composited at the wrong
/// place.
///
/// Every popup the panel opens - a combo box list, a menu, a completer list - is kept off
/// the desktop as well. A popup is a separate top-level window that does not inherit
/// `WA_DontShowOnScreen`, so @ref show_offscreen installs the guard that sets it on the
/// popup before Qt can map it (offscreen_popup_guard.hpp), and the capture composites the
/// popup into the panel from its widget geometry. Dialogs are the exception: they stay
/// ordinary windows (and stream as AR panels of their own).
///
/// The window state needs Qt Widgets (and a QApplication on the GUI thread); the
/// size parser is plain C++ and testable without either.

#include <string>

class QWidget;

namespace qar_qt_streaming
{

/// @name Size bounds
/// The default surface is a tall, narrow dock-shaped rectangle - the shape of a
/// typical tool dock - and an operator-chosen size is clamped per side so a
/// typo cannot ask for a 0-pixel panel or a 100000-pixel texture.
/// @{
inline constexpr int kDefaultOffscreenWidth = 480;
inline constexpr int kDefaultOffscreenHeight = 720;
inline constexpr int kMinOffscreenSide = 200;
inline constexpr int kMaxOffscreenSide = 2000;
/// @}

/// @brief @p side limited to kMinOffscreenSide .. kMaxOffscreenSide.
int clamp_offscreen_side(int side);

/// @brief What an operator-supplied "WIDTHxHEIGHT" decided.
struct OffscreenSizeChoice
{
  /// The size to use, each side already inside kMinOffscreenSide ..
  /// kMaxOffscreenSide. Always usable: unset, empty and invalid values all give the
  /// default.
  int width = kDefaultOffscreenWidth;
  int height = kDefaultOffscreenHeight;

  /// True when the value was a well-formed size (even one that had to be clamped), so
  /// a log can say "from <variable>" rather than "the default". False for unset/empty
  /// AND for an unparseable value.
  bool explicitly_chosen = false;

  /// True when at least one side of a well-formed size was outside the bounds and was
  /// moved to the nearest one.
  bool clamped = false;

  /// Non-empty exactly when a value WAS given but could not be used as written: it is
  /// not a size (the default is used) or it was out of range (the clamped size is
  /// used). The one warning the host should print - a silent fallback would let a
  /// typo like `480*720` run at the default without anyone knowing.
  std::string warning;
};

/// @brief Parse "WIDTHxHEIGHT" (decimal digits, `x` or `X`, optional spaces around
/// the whole value and around the `x`).
///
/// Pure and environment-free, like parse_panel_close_mode: the host passes the raw
/// text of its variable (`std::getenv`-style, null when unset). A null pointer, an
/// empty string and an all-whitespace string mean "unset" and give the default with
/// NO warning. Anything that is not exactly two unsigned decimal numbers joined by
/// an `x` ("480", "480x", "x720", "-480x720", "480.5x720", "480x720x3", "480*720",
/// "wide") gives the default plus one warning that names the value as typed. A
/// well-formed size has each side clamped to kMinOffscreenSide .. kMaxOffscreenSide;
/// a clamp is reported in @ref OffscreenSizeChoice::warning too. Numbers longer than
/// the range of an int saturate before they are clamped, so "99999999999x720" is
/// 2000x720, not an overflow.
///
/// @param value          raw text, or null when the variable is not set
/// @param default_width  used for unset/invalid values (itself clamped)
/// @param default_height used for unset/invalid values (itself clamped)
/// @param source         what to call the value in the warning, normally the
///                       environment variable's name; null/empty => "size"
OffscreenSizeChoice parse_offscreen_size(
  const char * value, int default_width = kDefaultOffscreenWidth,
  int default_height = kDefaultOffscreenHeight, const char * source = nullptr);

/// @brief Make @p widget a never-on-desktop window of exactly @p width x @p height
/// logical pixels and show it. See the file comment for what each step is for.
///
/// Call it ONCE, on the GUI thread, on a parentless widget that has not been shown;
/// calling it again on the same widget is harmless (it re-applies the same state and
/// shows the widget again, which is also how a hidden one is brought back). The sides
/// are clamped to kMinOffscreenSide .. kMaxOffscreenSide.
///
/// Installs the off-screen popup guard (install_offscreen_popup_guard) first.
///
/// The widget is NOT given a new window type: it stays a plain top-level window
/// (`Qt::Window`), never a `Qt::Dialog`, `Qt::Tool` or `Qt::Popup`, so the manager's
/// dialog/popup scans (collect_panel_overlays) can never mistake it for an overlay
/// of some other panel.
///
/// @return false (and does nothing) if @p widget is null or has a parent - a child
///         widget has no window of its own to keep off the desktop.
bool show_offscreen(QWidget * widget, int width, int height);

/// @brief Is @p widget in the state show_offscreen leaves it in: a top-level window
/// with `WA_DontShowOnScreen` set, `WA_QuitOnClose` clear and
/// `WindowDoesNotAcceptFocus` in its flags? (Says nothing about whether it is
/// currently visible - that is `QWidget::isVisible`.)
bool is_offscreen_panel(const QWidget * widget);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__OFFSCREEN_PANEL_HPP_
