// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/offscreen_popup_guard.hpp"

#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QToolTip>
#include <QVariant>
#include <QWidget>

#include <vector>

namespace qar_qt_streaming
{
namespace
{

/// GUI thread only, like everything the guard touches.
OffscreenPopupGuardStats g_stats;

/// The popup-class window kinds. windowType() masks the flags down to the one kind, so a
/// combo list (Qt::Popup | Qt::FramelessWindowHint) still answers Qt::Popup.
bool
is_popup_class(const QWidget * widget)
{
  const Qt::WindowType kind = widget->windowType();
  return kind == Qt::Popup || kind == Qt::ToolTip;
}

/// Qt's menu and tooltip animation helpers (qeffects.cpp): Qt::ToolTip windows parented to
/// the desktop's screen widget, shown on the desktop in place of a menu or tooltip while it
/// rolls or fades in. Their class names are the only public handle on them.
bool
is_effect_helper(const QWidget * widget)
{
  return widget->inherits("QRollEffect") || widget->inherits("QAlphaWidget");
}

/// The popup an effect helper animates. QRollEffect::run (and QAlphaWidget::run off Windows)
/// fakes the target's visibility - WA_WState_ExplicitShowHide on, WA_WState_Hidden off, not
/// yet isVisible() - and moves the helper onto the target's position right before show(),
/// which identifies the target. Null when no popup-class window is in that state there.
QWidget *
effect_target(const QWidget * effect)
{
  const QPoint where = effect->geometry().topLeft();
  const QWidgetList tops = QApplication::topLevelWidgets();
  for (QWidget * candidate : tops) {
    if (candidate == nullptr || candidate == effect || !is_popup_class(candidate) ||
      is_effect_helper(candidate))
    {
      continue;
    }
    const bool faked_visible = !candidate->isVisible() &&
      candidate->testAttribute(Qt::WA_WState_ExplicitShowHide) &&
      !candidate->testAttribute(Qt::WA_WState_Hidden);
    if (faked_visible && candidate->geometry().topLeft() == where) {
      return candidate;
    }
  }
  return nullptr;
}

void
keep_off_screen(QWidget * widget, bool effect)
{
  // On Show the widget is already isVisible(): QWidget::setAttribute then runs hide_sys()
  // and show_sys() for this attribute, both harmless on a native window that was never
  // mapped (the QWindow was never made visible, and show_sys() returns early once the
  // attribute is set). On Polish the widget is not visible yet and the attribute is just
  // recorded.
  widget->setAttribute(Qt::WA_DontShowOnScreen, true);
  widget->setProperty(kOffscreenPopupProperty, true);
  if (effect) {
    ++g_stats.kept_effects;
  } else {
    ++g_stats.kept_popups;
  }
}

/// No Q_OBJECT: it has no signals, slots or properties, so it needs no moc.
class OffscreenPopupGuard : public QObject
{
public:
  explicit OffscreenPopupGuard(QObject * parent)
  : QObject(parent)
  {
  }

protected:
  bool
  eventFilter(QObject * watched, QEvent * event) override
  {
    const QEvent::Type type = event->type();
    if (type != QEvent::Polish && type != QEvent::Show) {
      return false;  // the hot path: one integer compare per event in the application
    }
    if (!watched->isWidgetType()) {
      return false;
    }
    auto * widget = static_cast<QWidget *>(watched);
    if (!widget->isWindow() || widget->testAttribute(Qt::WA_DontShowOnScreen) ||
      !is_popup_class(widget))
    {
      return false;
    }
    if (is_owned_by_offscreen_panel(widget)) {
      keep_off_screen(widget, false);
      return false;
    }
    if (is_effect_helper(widget)) {
      QWidget * target = effect_target(widget);
      if (target != nullptr &&
        (is_offscreen_popup(target) || is_owned_by_offscreen_panel(target)))
      {
        if (!is_offscreen_popup(target)) {
          keep_off_screen(target, false);  // polished before it was owned: mark it now
        }
        keep_off_screen(widget, true);
      }
    }
    return false;  // never consumes: Qt delivers the event as if no filter were installed
  }
};

QPointer<OffscreenPopupGuard> &
guard_instance()
{
  static QPointer<OffscreenPopupGuard> guard;
  return guard;
}

}  // namespace

bool
install_offscreen_popup_guard()
{
  auto * app = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (app == nullptr || QThread::currentThread() != app->thread()) {
    return false;
  }
  QPointer<OffscreenPopupGuard> & guard = guard_instance();
  if (guard.isNull()) {  // also after a QApplication was destroyed and made again
    guard = new OffscreenPopupGuard(app);
    app->installEventFilter(guard.data());
  }
  return true;
}

bool
offscreen_popup_guard_installed()
{
  const QPointer<OffscreenPopupGuard> & guard = guard_instance();
  return !guard.isNull() && guard->parent() == QCoreApplication::instance();
}

OffscreenPopupGuardStats
offscreen_popup_guard_stats()
{
  return g_stats;
}

QWidget *
popup_owner_widget(const QWidget * popup)
{
  if (popup == nullptr) {
    return nullptr;
  }
  if (QWidget * parent = popup->parentWidget()) {
    return parent;
  }
  return popup->focusProxy();
}

bool
is_offscreen_popup(const QWidget * widget)
{
  return widget != nullptr && widget->testAttribute(Qt::WA_DontShowOnScreen) &&
         widget->property(kOffscreenPopupProperty).toBool();
}

bool
is_owned_by_offscreen_panel(const QWidget * window)
{
  const QWidget * current = window;
  // Bounded: a popup chain is a handful deep, and a cycle of focus proxies must not hang
  // the GUI thread.
  for (int depth = 0; current != nullptr && depth < 16; ++depth) {
    const QWidget * owner = popup_owner_widget(current);
    if (owner == nullptr) {
      return false;
    }
    const QWidget * host = owner->window();
    if (host == nullptr || host == current) {
      return false;
    }
    if (is_offscreen_panel(host)) {
      return true;
    }
    if (!is_popup_class(host)) {
      return false;  // a main window, dialog or dock: a visible window, not ours to hide
    }
    if (is_offscreen_popup(host)) {
      return true;  // a submenu or list opened from a popup that is already kept off
    }
    current = host;
  }
  return false;
}

std::uint32_t
close_offscreen_popups(QWidget * panel_root)
{
  if (panel_root == nullptr || QCoreApplication::instance() == nullptr) {
    return 0;
  }
  if (QApplication::activePopupWidget() == nullptr && !QToolTip::isVisible()) {
    return 0;
  }
  const std::vector<QWidget *> popups = collect_panel_popups(panel_root, true);
  std::uint32_t closed = 0;
  // Innermost first, so a submenu goes before the menu that opened it.
  for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
    QPointer<QWidget> popup(*it);
    if (popup.isNull() || !popup->isVisible() || !is_offscreen_popup(popup.data())) {
      continue;
    }
    popup->close();
    ++closed;
  }
  return closed;
}

}  // namespace qar_qt_streaming
