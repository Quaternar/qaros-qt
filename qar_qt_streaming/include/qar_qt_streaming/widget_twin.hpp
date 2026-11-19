// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__WIDGET_TWIN_HPP_
#define QAR_QT_STREAMING__WIDGET_TWIN_HPP_

/// @file widget_twin.hpp
/// @brief Keeps a second widget tree showing the state of a first one and gives its input back.
///
/// A panel that is streamed to an AR device with enlarged controls (enlarged_controls_style.hpp)
/// needs a copy of itself: the style is a property of the widgets, so the copy has to be a second
/// tree built under it. WidgetTwinBinder keeps the two trees consistent without a second owner of
/// the state. The host builds both trees with the same code and names the pairs; the original
/// stays the only owner of the state and its slots stay connected to the original only.

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

class QWidget;

namespace qar_qt_streaming
{

/// The most blocks the twin of a QPlainTextEdit holds: the last ones of the original.
inline constexpr int kTwinTextBlocks = 200;

/// The least time in milliseconds between two copies of the text of a QPlainTextEdit to its twin.
inline constexpr int kTwinTextPeriodMs = 250;

/// @brief Keeps a second widget tree (the twin, for example an off-screen AR copy) showing the
/// state of a first one (the original) and carries the input on the twin out on the original.
///
/// The ORIGINAL is the only owner of the state. A user action on a twin (a click, a choice, a
/// step, a chosen row, an edit) is carried out on its original, so the slots connected to the
/// original run as they do for a user of the original. sync() copies what the originals show to
/// the twins; it polls, so it also follows changes made with the original's signals blocked. A
/// twin signal during sync() is never forwarded, and an action on a twin whose original is
/// disabled is refused. Qt GUI thread only; nothing here waits.
///
/// What sync() copies depends on the class of the twin: every pair gets the enabled state, the
/// explicit hide and the tool tip; a button its text, checkable state and checked state; a group
/// box its title, checkable state and checked state; a label its text; a combo box its items
/// (text, user data, tool tip), current index and, when editable, its text; a line edit its text
/// (unless the twin has the focus), placeholder and read-only state; a spin box its range, step,
/// affixes, decimals and value; a slider its range and value; a list widget its rows (text and
/// the user, tool tip, foreground and check state roles, flags) and current row; a plain text
/// edit the last kTwinTextBlocks blocks while it is visible. Any other class gets the common
/// state only.
class WidgetTwinBinder : public QObject
{
public:
  explicit WidgetTwinBinder(QObject * parent = nullptr);

  /// Calls release().
  ~WidgetTwinBinder() override;

  WidgetTwinBinder(const WidgetTwinBinder &) = delete;
  WidgetTwinBinder & operator=(const WidgetTwinBinder &) = delete;

  /// @brief Binds a pair whose class sync() and the forward know.
  /// @return false (nothing bound) when either widget is null, the two class names differ, or
  /// either widget is bound already.
  bool bind(QWidget * original, QWidget * twin);

  /// @brief Binds a pair whose state the host copies itself (a widget class of its own).
  /// sync() still copies the enabled state, the explicit hide and the tool tip, then calls
  /// @p copy. Nothing of the twin is forwarded.
  /// @return false (nothing bound) when either widget or @p copy is null or either widget is
  /// bound already.
  bool bind_custom(
    QWidget * original, QWidget * twin,
    std::function<void(QWidget & original, QWidget & twin)> copy);

  /// @brief One pass over every pair whose two widgets still exist.
  /// @return the number of writes to twins (a write only happens when the values differ).
  int sync();

  /// @brief Ends the forwarding and drops every pair; the twin tree may die after this.
  void release();

  /// @brief Called after a twin action was carried out on its original, with the original and
  /// a word: "clicked", "chosen", "stepped", "row chosen" or "edited". A single typed character
  /// is carried out but not reported; the end of an edit is.
  void set_forward_observer(std::function<void(QWidget & original, const char * what)> observer);

  /// @brief Replaces the clock (milliseconds, monotonic) that the text copy period uses; an
  /// empty function restores the steady clock.
  void set_time_source(std::function<std::int64_t()> now_ms);

  struct Counters
  {
    std::uint64_t passes = 0;
    std::uint64_t writes = 0;
    std::uint64_t forwarded = 0;   ///< twin actions that reached an original and did something
    std::uint64_t refused = 0;     ///< twin actions on an original that is disabled
    std::size_t pairs = 0;
    std::size_t plain_pairs = 0;   ///< pairs of a class with no kind (the common state only)
  };

  Counters counters() const;

  /// True while a twin action is carried out on its original.
  bool forwarding() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__WIDGET_TWIN_HPP_
