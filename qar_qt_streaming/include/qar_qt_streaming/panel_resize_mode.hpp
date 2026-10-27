// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__PANEL_RESIZE_MODE_HPP_
#define QAR_QT_STREAMING__PANEL_RESIZE_MODE_HPP_

/// @file panel_resize_mode.hpp
/// @brief How a streamed panel follows a size change of its window, and the pure parser for
/// the `QAR_PANEL_RESIZE` switch that chooses it.
///
/// Qt-free and environment-free like panel_close_mode.hpp: the parser takes the raw text (the
/// host passes `std::getenv(...)` in).

#include <string>

namespace qar_qt_streaming
{

/// @brief What a size change does to the QarOS panel behind a stream.
enum class PanelResizeMode
{
  /// **The default.** The QarOS panel stays; its video sender is released
  /// (IQarPanelSenderAdapter::detach) and a new sender of the new size is attached to the SAME
  /// panel, which keeps its common name. Used for a sender that can detach; a sender that cannot
  /// takes the NewPanel path.
  Swap,

  /// A size change makes a second QarOS panel: the common name of every panel of the stream
  /// carries the buffer size ("@<w>x<h>"), the old panel is hidden and kept (see
  /// QtPanelStreamingConfig::max_replaced_panels), and a return to a size that is kept shows
  /// the kept panel again. Selected with `QAR_PANEL_RESIZE=new-panel`.
  NewPanel
};

/// @brief "swap" or "new-panel" - the spelling QAR_PANEL_RESIZE accepts, and the one the logs
/// print.
const char * to_string(PanelResizeMode mode);

/// @brief The environment variable that selects the mode.
inline constexpr const char * kPanelResizeModeEnv = "QAR_PANEL_RESIZE";

/// @brief What a QAR_PANEL_RESIZE value decided.
struct PanelResizeModeChoice
{
  /// The mode to run with. Always usable: an unset or unrecognised value is
  /// PanelResizeMode::Swap, the default.
  PanelResizeMode mode = PanelResizeMode::Swap;

  /// True when the value named a mode explicitly. False for unset/empty AND for an
  /// unrecognised value.
  bool explicitly_chosen = false;

  /// Non-empty exactly when a value WAS given but is not a mode: the one warning the host
  /// should print.
  std::string warning;
};

/// @brief Parse a QAR_PANEL_RESIZE value.
///
/// Case-insensitive, surrounding whitespace ignored: `swap`, `new-panel` (also `new_panel`
/// and `newpanel`). A null pointer, an empty string and an all-whitespace string mean "unset"
/// and give the default WITHOUT a warning. Anything else gives the default plus one
/// @ref PanelResizeModeChoice::warning naming the value and the accepted words.
PanelResizeModeChoice parse_panel_resize_mode(const char * value);

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__PANEL_RESIZE_MODE_HPP_
