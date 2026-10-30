// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_CLOSE_MODE_HPP_
#define QAR_QT_STREAMING__PANEL_CLOSE_MODE_HPP_

/// @file panel_close_mode.hpp
/// @brief What happens to a streamed panel when its Qt window closes, and the
/// pure parser for the `QAR_PANEL_CLOSE_MODE` switch that chooses it.
///
/// Qt-free and environment-free on purpose, like qar_qar_adapter's env_config:
/// the parser takes the raw text (the host passes `std::getenv(...)` in), so it
/// is exactly testable without a QApplication, a session or a process
/// environment.

#include <string>

namespace qar_qt_streaming
{

/// @brief How the manager retires a panel whose source window is gone.
enum class PanelCloseMode
{
  /// **The default.** Close the QarOS GUI panel AND destroy its video sender, then
  /// forget the stream entirely. Re-opening the window later is an ordinary fresh
  /// add: a new GUI panel on a new sender. The teardown order is the adapter's
  /// (IQarPanelSenderAdapter::close): input subscriptions first, then the panel,
  /// then the sender it consumed.
  ///
  /// The current QarOS runtime survives destroying a stream mid-session, so
  /// nothing has to keep the sender alive.
  Destroy,

  /// The runtime escape hatch: the stream and its sender are KEPT, and the AR
  /// panel shows a "window closed — you may close this AR panel" notice (the
  /// tombstone, panel_tombstone.hpp) until the session ends. Selected with
  /// `QAR_PANEL_CLOSE_MODE=notice` for a runtime on which destroying a sender
  /// mid-session is not safe.
  Notice,

  /// Nothing is closed while the session runs: a Qt-side close PARKS the stream. The AR
  /// panel is hidden (`qar_gui_panels_set_state`), capture stops and input is dropped, and
  /// the QarOS panel, the video sender and the subscriptions are kept, with no notice shown.
  /// Adding the same key again revives the parked stream onto its window and shows it, so
  /// no second QarOS panel is created. At session end nothing is closed either: each QarOS
  /// panel is hidden and left in place under its name, its video sender is released, and the
  /// next session finds it; only a sender that cannot detach is closed.
  /// Selected with `QAR_PANEL_CLOSE_MODE=park` for a runtime on which closing a panel
  /// mid-session is not safe.
  Park
};

/// @brief "destroy", "notice" or "park" - the spelling QAR_PANEL_CLOSE_MODE accepts, and
/// the one the logs print.
const char * to_string(PanelCloseMode mode);

/// @brief The environment variable that selects the mode.
inline constexpr const char * kPanelCloseModeEnv = "QAR_PANEL_CLOSE_MODE";

/// @brief What a QAR_PANEL_CLOSE_MODE value decided.
struct PanelCloseModeChoice
{
  /// The mode to run with. Always usable: an unset or unrecognised value is
  /// PanelCloseMode::Destroy, the default.
  PanelCloseMode mode = PanelCloseMode::Destroy;

  /// True when the value named a mode explicitly (so a log can say "chosen by
  /// QAR_PANEL_CLOSE_MODE" rather than "default"). False for unset/empty AND for
  /// an unrecognised value.
  bool explicitly_chosen = false;

  /// Non-empty exactly when a value WAS given but is not a mode: the one warning
  /// the host should print. A silent fallback would let a typo like
  /// `QAR_PANEL_CLOSE_MODE=notise` run in the wrong mode without anyone knowing.
  std::string warning;
};

/// @brief Parse a QAR_PANEL_CLOSE_MODE value.
///
/// Case-insensitive, surrounding whitespace ignored: `destroy`, `notice` or `park`.
/// A null pointer, an empty string and an all-whitespace string mean "unset" and
/// give the default WITHOUT a warning. Anything else gives the default plus one
/// @ref PanelCloseModeChoice::warning naming the value and the accepted words.
PanelCloseModeChoice parse_panel_close_mode(const char * value);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_CLOSE_MODE_HPP_
