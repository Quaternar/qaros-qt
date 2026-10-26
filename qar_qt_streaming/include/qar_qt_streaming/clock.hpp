// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__CLOCK_HPP_
#define QAR_QT_STREAMING__CLOCK_HPP_

#include <chrono>

namespace qar_qt_streaming
{

/// @brief Monotonic time source seam for per-panel FPS pacing.
///
/// The manager reads "now" ONCE per tick and threads it into every stream as a
/// plain @c double (seconds), so pacing is deterministically unit-testable
/// without sleeping. Production uses @ref SteadyClock; tests inject a fake clock
/// they advance by hand.
///
/// A local twin of qar_ogre_streaming's IClock rather than a reuse of it:
/// depending on that library would drag Ogre into a Qt-only library.
class IClock
{
public:
  virtual ~IClock() = default;

  /// @brief Monotonic time in seconds. Only differences are meaningful.
  virtual double now_seconds() const = 0;
};

/// @brief Default IClock backed by std::chrono::steady_clock (monotonic).
class SteadyClock : public IClock
{
public:
  double now_seconds() const override
  {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - epoch_).count();
  }

private:
  std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__CLOCK_HPP_
