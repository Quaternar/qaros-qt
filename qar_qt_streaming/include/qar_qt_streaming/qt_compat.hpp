// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QT_COMPAT_HPP_
#define QAR_QT_STREAMING__QT_COMPAT_HPP_

/// @file qt_compat.hpp
/// @brief The few places where the Qt 5.15 and the Qt 6 API of input events differ.
///
/// The injector (qt_panel_input_injector.hpp) and its tests call the helpers below and contain
/// no `#if QT_VERSION`. Everything that depends on the major version of Qt lives in this one
/// header:
///
///   * the touch device is a `QTouchDevice` in Qt 5 and a `QPointingDevice` in Qt 6, and Qt 6
///     also wants a pointing device of type Mouse for mouse events;
///   * a touch point is a `QTouchEvent::TouchPoint` with setters in Qt 5 and a `QEventPoint`
///     in Qt 6, which has no public setter for the start and last positions or the pressure;
///   * the constructors of `QTouchEvent`, `QMouseEvent` and `QHoverEvent` take other arguments;
///   * `QTouchEvent::touchPoints()` is `points()` in Qt 6;
///   * a size of a Qt container is an `int` in Qt 5 and a `qsizetype` in Qt 6.

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTouchEvent>
#include <QWidget>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define QAR_QT_COMPAT_QT6 1
#include <QEventPoint>
#include <QPointingDevice>
#else
#define QAR_QT_COMPAT_QT6 0
#include <QTouchDevice>
#include <QWindow>
#endif

#include <memory>
#include <vector>

namespace qar_qt_streaming
{
namespace qt_compat
{

#if QAR_QT_COMPAT_QT6
using TouchDevice = QPointingDevice;
using TouchPoint = QEventPoint;
#else
using TouchDevice = QTouchDevice;
using TouchPoint = QTouchEvent::TouchPoint;
#endif

/// @brief The size of a Qt container as an `int`. Every container the injector and its tests
/// measure holds a handful of items.
template<typename Container>
int
qt_size(const Container & container)
{
  return static_cast<int>(container.size());
}

/// @brief The phase of one touch point.
enum class TouchPhase
{
  Pressed,
  Moved,
  Stationary,
  Released
};

/// @brief One touch point of a touch event, in the coordinates of the receiving widget.
struct TouchPointSpec
{
  int id = 0;
  TouchPhase phase = TouchPhase::Pressed;
  QPointF local;          ///< position in the receiving widget
  QPointF start_local;    ///< where the point went down, in the receiving widget
  QPointF last_local;     ///< where the point was at the previous event, in the receiving widget
  QPointF window;         ///< position in the top-level widget of the receiver (Qt 6 scene)
  QPointF screen;         ///< position on the screen
  QPointF start_screen;
  QPointF last_screen;
  double pressure = 1.0;
};

/// @brief The input devices of one injector: a touch screen, and in Qt 6 a mouse as well.
///
/// They are not registered with the window system: registration is for devices that the
/// platform delivers spontaneous events from, and these never do. Widgets only ever inspect
/// the type and the capabilities of a device.
class InputDevices
{
public:
  InputDevices()
#if QAR_QT_COMPAT_QT6
  : touch_(new QPointingDevice(
        QStringLiteral("QarOS panel pointer"), kTouchSystemId,
        QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Position, 10, 0)),
    mouse_(new QPointingDevice(
        QStringLiteral("QarOS panel mouse"), kMouseSystemId,
        QInputDevice::DeviceType::Mouse, QPointingDevice::PointerType::Generic,
        QInputDevice::Capability::Position, 1, 3))
  {
  }
#else
  : touch_(std::make_unique<QTouchDevice>())
  {
    touch_->setName(QStringLiteral("QarOS panel pointer"));
    touch_->setType(QTouchDevice::TouchScreen);
    touch_->setCapabilities(QTouchDevice::Position);
    touch_->setMaximumTouchPoints(10);
  }
#endif

  /// Not const: the QTouchEvent of Qt 5 takes a non-const device.
  TouchDevice * touch() const {return touch_.get();}

#if QAR_QT_COMPAT_QT6
  const QPointingDevice * mouse() const {return mouse_.get();}
#endif

private:
#if QAR_QT_COMPAT_QT6
  static constexpr qint64 kTouchSystemId = 0x51415201;
  static constexpr qint64 kMouseSystemId = 0x51415202;
#endif
  std::unique_ptr<TouchDevice> touch_;
#if QAR_QT_COMPAT_QT6
  std::unique_ptr<QPointingDevice> mouse_;
#endif
};

/// @brief A mouse event that comes from the application, not from the window system.
///
/// @p local is relative to the receiving widget, @p window to its top-level widget and
/// @p screen to the screen. Qt 5 marks the event as synthesized by the application; Qt 6 has
/// no such source on a public constructor, so the event carries the mouse device of
/// @p devices.
inline std::unique_ptr<QMouseEvent>
make_mouse_event(
  const InputDevices & devices, QEvent::Type type, const QPointF & local, const QPointF & window,
  const QPointF & screen, Qt::MouseButton button, Qt::MouseButtons buttons)
{
#if QAR_QT_COMPAT_QT6
  return std::make_unique<QMouseEvent>(
    type, local, window, screen, button, buttons, Qt::NoModifier, devices.mouse());
#else
  (void)devices;
  return std::make_unique<QMouseEvent>(
    type, local, window, screen, button, buttons, Qt::NoModifier,
    Qt::MouseEventSynthesizedByApplication);
#endif
}

/// @brief A hover event. @p position and @p old_position are relative to the receiving widget
/// ((-1, -1) is "outside"); @p screen is only used by Qt 6.
inline std::unique_ptr<QHoverEvent>
make_hover_event(
  const InputDevices & devices, QEvent::Type type, const QPointF & position,
  const QPointF & screen, const QPointF & old_position)
{
#if QAR_QT_COMPAT_QT6
  return std::make_unique<QHoverEvent>(
    type, position, screen, old_position, Qt::NoModifier, devices.mouse());
#else
  (void)devices;
  (void)screen;
  return std::make_unique<QHoverEvent>(type, position, old_position);
#endif
}

/// @brief A touch event for the widget @p target with the points @p specs.
///
/// Qt 5 keeps every position and the pressure of a point. Qt 6 builds each point from its
/// position in the top-level widget (the scene position) and its screen position, the two
/// positions public API can set. A widget derives its own position from either one with
/// `mapFromGlobal()` or `mapFrom(window(), ...)`. The positions of the point in the receiving
/// widget, its press position and its last position are not settable in public API and are not
/// meaningful on a point built this way.
inline std::unique_ptr<QTouchEvent>
make_touch_event(
  const InputDevices & devices, QEvent::Type type, QWidget * target,
  const std::vector<TouchPointSpec> & specs)
{
#if QAR_QT_COMPAT_QT6
  (void)target;
  QList<QEventPoint> points;
  for (const TouchPointSpec & spec : specs) {
    QEventPoint::State state = QEventPoint::State::Stationary;
    switch (spec.phase) {
      case TouchPhase::Pressed:
        state = QEventPoint::State::Pressed;
        break;
      case TouchPhase::Moved:
        state = QEventPoint::State::Updated;
        break;
      case TouchPhase::Stationary:
        state = QEventPoint::State::Stationary;
        break;
      case TouchPhase::Released:
        state = QEventPoint::State::Released;
        break;
    }
    points.append(QEventPoint(spec.id, state, spec.window, spec.screen));
  }
  return std::make_unique<QTouchEvent>(type, devices.touch(), Qt::NoModifier, points);
#else
  QList<QTouchEvent::TouchPoint> points;
  Qt::TouchPointStates states;
  for (const TouchPointSpec & spec : specs) {
    Qt::TouchPointState state = Qt::TouchPointStationary;
    switch (spec.phase) {
      case TouchPhase::Pressed:
        state = Qt::TouchPointPressed;
        break;
      case TouchPhase::Moved:
        state = Qt::TouchPointMoved;
        break;
      case TouchPhase::Stationary:
        state = Qt::TouchPointStationary;
        break;
      case TouchPhase::Released:
        state = Qt::TouchPointReleased;
        break;
    }
    QTouchEvent::TouchPoint point(spec.id);
    point.setState(Qt::TouchPointStates(state));
    point.setPos(spec.local);
    point.setStartPos(spec.start_local);
    point.setLastPos(spec.last_local);
    // For widgets the scene position IS the screen position (QTouchEvent docs).
    point.setScenePos(spec.screen);
    point.setStartScenePos(spec.start_screen);
    point.setLastScenePos(spec.last_screen);
    point.setScreenPos(spec.screen);
    point.setStartScreenPos(spec.start_screen);
    point.setLastScreenPos(spec.last_screen);
    point.setPressure(spec.pressure);
    points.append(point);
    states |= state;
  }
  auto event = std::make_unique<QTouchEvent>(
    type, devices.touch(), Qt::NoModifier, states, points);
  event->setWindow(target->window()->windowHandle());
  event->setTarget(target);
  return event;
#endif
}

/// @brief The points of a touch event.
inline const QList<TouchPoint> &
touch_points(const QTouchEvent & event)
{
#if QAR_QT_COMPAT_QT6
  return event.points();
#else
  return event.touchPoints();
#endif
}

/// @brief The position of a mouse event in the receiving widget.
inline QPointF
mouse_local_position(const QMouseEvent & event)
{
#if QAR_QT_COMPAT_QT6
  return event.position();
#else
  return event.localPos();
#endif
}

/// @brief Whether a touch point is released.
inline bool
is_released(const TouchPoint & point)
{
#if QAR_QT_COMPAT_QT6
  return point.state() == QEventPoint::State::Released;
#else
  return point.state() == Qt::TouchPointReleased;
#endif
}

/// @brief The object that Qt names as the owner of a touch sequence after it delivered the
/// event: the target of the event in Qt 5, the exclusive grabber of its first point in Qt 6.
/// An event that is sent with `sendEvent` has no exclusive grabber in Qt 6, so the result is
/// null there; the widget the event was last delivered to is the owner.
inline QObject *
touch_owner_named_by_qt(const QTouchEvent & event)
{
#if QAR_QT_COMPAT_QT6
  const auto & points = event.points();
  return points.isEmpty() ? nullptr : event.exclusiveGrabber(points.first());
#else
  return event.target();
#endif
}

/// @brief Make the top-level window of @p widget the active window.
inline void
activate_window(QWidget * widget)
{
#if QAR_QT_COMPAT_QT6
  widget->activateWindow();
#else
  QApplication::setActiveWindow(widget);
#endif
}

/// @brief Let high-DPI scaling produce fractional device pixel ratios (1.25, 1.5), as Qt 6
/// does by default and Qt 5.15 does only when it is asked to. Call before the QApplication is
/// constructed.
inline void
enable_fractional_high_dpi()
{
#if QAR_QT_COMPAT_QT6
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#else
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif
}

}  // namespace qt_compat
}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__QT_COMPAT_HPP_
