// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__OFFSCREEN_POPUP_GUARD_HPP_
#define QAR_QT_STREAMING__OFFSCREEN_POPUP_GUARD_HPP_

/// @file offscreen_popup_guard.hpp
/// @brief Keeps every popup an OFF-SCREEN panel (offscreen_panel.hpp) opens - combo box
/// lists, menus and their submenus, completer lists, tooltip windows that have the panel
/// as their parent - off the desktop, so it exists only in that panel's AR image.
///
/// show_offscreen() keeps the panel's own window off the desktop, but a popup is a
/// separate top-level window of its own, and Qt maps it on the desktop unless THAT
/// widget carries Qt::WA_DontShowOnScreen (QWidgetPrivate::show_sys). Mapped, it is a
/// real top-most Windows popup at the panel's virtual position (the primary screen's
/// top-left corner): Qt's Windows plugin calls SetForegroundWindow on it when the
/// application has no focus window and grabs the OS mouse for it, and Qt closes every
/// popup when the application loses focus - so it lives by desktop rules the AR operator
/// cannot see or control.
///
/// The guard is one application-wide event filter. On QEvent::Polish and QEvent::Show -
/// both delivered before Qt maps a native window - it sets WA_DontShowOnScreen (and the
/// kOffscreenPopupProperty marker) on a Qt::Popup / Qt::ToolTip window owned by an
/// off-screen panel. Qt then never maps it, never activates it and never gives it the
/// OS mouse; it stays isVisible(), QApplication::activePopupWidget() still routes input
/// to it, and QWidget::render() still paints it, so the capture composites it into the
/// panel and the AR injector operates it exactly as before. Visible panels' popups are
/// never touched. The filter never consumes an event.
///
/// Not covered, by design: dialogs (they stream as AR panels of their own and stay on the
/// desktop), Qt::Tool windows, and tooltips on Windows (QToolTip parents its label to the
/// desktop there, so the guard cannot tell whose it is and the capture does not attribute
/// it to a panel either; AR input cannot raise a tooltip, Qt arms its tooltip timer only
/// for spontaneous mouse moves).
///
/// GUI thread only. Kept free of <QObject>/<QWidget>: panel_pointer_state.hpp declares a
/// member named emit(), which Qt's `emit` keyword breaks if it is seen first.

#include <QPoint>

#include <cstdint>

class QWidget;

namespace qar_qt_streaming
{

/// Dynamic property the guard sets (true) on every window it kept off the desktop.
inline constexpr const char * kOffscreenPopupProperty = "_qar_offscreen_popup";

/// @brief Install the guard (once per QApplication; later calls are no-ops).
/// show_offscreen() calls it. @return false without a QApplication or off its thread.
bool install_offscreen_popup_guard();

/// @brief Is the guard installed on the current QApplication?
bool offscreen_popup_guard_installed();

/// Cumulative counts since the process started (diagnostics and tests compare deltas).
struct OffscreenPopupGuardStats
{
  std::uint64_t kept_popups = 0;   ///< popup-class windows kept off the desktop
  std::uint64_t kept_effects = 0;  ///< Qt menu/tooltip animation windows kept off with them
};
OffscreenPopupGuardStats offscreen_popup_guard_stats();

/// @brief The widget a popup-class window belongs to: its parentWidget(), or - for a
/// parentless one such as QCompleter's list - its focusProxy(). Null when neither.
QWidget * popup_owner_widget(const QWidget * popup);

/// @brief Does @p window (a top-level Qt::Popup / Qt::ToolTip) belong to an off-screen
/// panel? Walks popup_owner_widget()->window(): an is_offscreen_panel() window answers
/// yes, a popup the guard already kept off (a submenu's menu) answers yes, any other
/// window answers no. Bounded walk.
bool is_owned_by_offscreen_panel(const QWidget * window);

/// @brief Did the guard keep @p widget off the desktop (marker set, WA_DontShowOnScreen on)?
bool is_offscreen_popup(const QWidget * widget);

/// @brief Close every open popup of @p panel_root's tree that the guard keeps off the
/// desktop, innermost first. Called when the panel stops being operable from AR
/// (QtPanelInputInjector::reset), so an invisible list cannot hold the application in
/// popup mode. Popups on the desktop are never touched. @return how many were closed.
std::uint32_t close_offscreen_popups(QWidget * panel_root);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__OFFSCREEN_POPUP_GUARD_HPP_
