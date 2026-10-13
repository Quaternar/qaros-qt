// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for off-screen panels and for the close modes. The functions are compiled and linked and never
// run; their parameters stand for what the host owns.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <QWidget>

#include <cstdio>

namespace qqs = qar_qt_streaming;

void snippet_offscreen(qqs::QtPanelStreamManager * manager, QWidget * hidden, const char * size_text)
{
  // [qar-snippet:offscreen]
  // size_text is the text of a setting of your own, WIDTHxHEIGHT, or null when nothing is set.
  const qqs::OffscreenSizeChoice choice = qqs::parse_offscreen_size(size_text, 480, 720, "the size of the hidden panel");
  if (!choice.warning.empty()) {
    std::printf("%s\n", choice.warning.c_str());   // not a size, or a side outside 200 to 2000 that was clamped
  }
  // Shown to Qt and never to the desktop; false for a null widget and for a widget that has a parent.
  if (qqs::show_offscreen(hidden, choice.width, choice.height)) {
    manager->add_panel("hidden", hidden, qqs::PanelStreamOptions{});
  }
  std::printf("off-screen: %d\n", qqs::is_offscreen_panel(hidden) ? 1 : 0);
  // [qar-snippet-end:offscreen]
}

void snippet_close_mode(const char * mode_text, qqs::QtPanelStreamingConfig & config)
{
  // [qar-snippet:close_mode]
  // mode_text is the text of a setting of your own, or null when nothing is set. The library reads no environment
  // variable itself; kPanelCloseModeEnv only names the variable that a host may use for it.
  const qqs::PanelCloseModeChoice choice = qqs::parse_panel_close_mode(mode_text);
  if (!choice.warning.empty()) {
    std::printf("%s\n", choice.warning.c_str());   // a word that is neither destroy nor notice
  }
  config.close_mode = choice.mode;                 // PanelCloseMode::Destroy or PanelCloseMode::Notice
  std::printf("%s: %s (%s)\n", qqs::kPanelCloseModeEnv, qqs::to_string(choice.mode),
    choice.explicitly_chosen ? "chosen" : "default");
  // [qar-snippet-end:close_mode]
}

void snippet_close_calls(qqs::QtPanelStreamManager * manager)
{
  // [qar-snippet:close_calls]
  manager->close_panel("settings", "the window was closed");    // follows the close mode of the manager
  manager->remove_panel("settings");                            // the same, with no reason given
  manager->tombstone_panel("status", "the window was closed");  // the notice, whatever the close mode is
  if (manager->is_tombstoned("status")) {
    std::printf("%u panels show a notice\n", static_cast<unsigned>(manager->tombstoned_panel_keys().size()));
  }
  manager->note_ar_panel_closed("status");   // the operator closed the panel in AR; a source of input reports it for you
  manager->clear();                          // end of the session: closes every panel that is still held, once
  // [qar-snippet-end:close_calls]
}
