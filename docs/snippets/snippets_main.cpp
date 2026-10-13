// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Entry point of the documentation snippet target. Every snippet function is declared here and
// called inside `if (argc > 1000)`: the code is compiled and linked, all references resolve, and
// nothing runs. The arguments are null pointers or empty objects, which the functions would never
// get from a host.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/video_frame.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace qqs = qar_qt_streaming;

// manager_snippets.cpp
std::unique_ptr<qqs::QtPanelStreamManager> snippet_manager_config(
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory,
  std::shared_ptr<qar_ar_core::IQarPanelInputSource> input,
  qar_ar_core::PanelDiagnosticsSink * sink, qqs::IQarPanelInputSink * input_sink);
void snippet_manager_use(qqs::QtPanelStreamManager * manager, QWidget * widget);
void snippet_diagnostics(qqs::QtPanelStreamManager * manager);
void snippet_use_sink(qqs::QtPanelStreamManager * manager);

// stream_snippets.cpp
qqs::PanelStreamOptions snippet_stream_options();
void snippet_capture(QWidget * widget, QWidget * camera_view, std::shared_ptr<qqs::LatestVideoFrame> camera);
void snippet_injector(QWidget * widget);
void snippet_pointer_state();

// geometry_snippets.cpp
void snippet_readability(qqs::PanelStreamOptions & options);
void snippet_geometry();
void snippet_hit_test(const qar_ar_core::PanelPlacement & placement);

// lifecycle_snippets.cpp
void snippet_offscreen(qqs::QtPanelStreamManager * manager, QWidget * hidden, const char * size_text);
void snippet_close_mode(const char * mode_text, qqs::QtPanelStreamingConfig & config);
void snippet_close_calls(qqs::QtPanelStreamManager * manager);

// video_snippets.cpp
void snippet_decode(
  const std::uint8_t * rgb, std::uint32_t width, std::uint32_t height, const std::uint8_t * jpeg, std::size_t jpeg_size,
  std::shared_ptr<qqs::LatestVideoFrame> mailbox);
void snippet_video_panel(
  qqs::QtPanelStreamManager * manager, qqs::VideoPanelWidget * view, std::shared_ptr<qqs::LatestVideoFrame> mailbox);
void snippet_region_source(
  qqs::QtPanelStreamManager * manager, QWidget * render_view, std::shared_ptr<qqs::LatestVideoFrame> mailbox);

int main(int argc, char** argv) {
  (void)argv;
  if (argc > 1000) {
    qqs::QtPanelStreamingConfig config;
    qqs::PanelStreamOptions options;
    qar_ar_core::PanelPlacement placement;
    const std::shared_ptr<qqs::LatestVideoFrame> mailbox;

    snippet_manager_config(nullptr, nullptr, nullptr, nullptr);
    snippet_manager_use(nullptr, nullptr);
    snippet_diagnostics(nullptr);
    snippet_use_sink(nullptr);

    snippet_stream_options();
    snippet_capture(nullptr, nullptr, mailbox);
    snippet_injector(nullptr);
    snippet_pointer_state();

    snippet_readability(options);
    snippet_geometry();
    snippet_hit_test(placement);

    snippet_offscreen(nullptr, nullptr, nullptr);
    snippet_close_mode(nullptr, config);
    snippet_close_calls(nullptr);

    snippet_decode(nullptr, 0, 0, nullptr, 0, mailbox);
    snippet_video_panel(nullptr, nullptr, mailbox);
    snippet_region_source(nullptr, nullptr, mailbox);
  }
  return 0;
}
