// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for the manager: its configuration, the calls of a host, the input sink and the diagnostics.
// The functions are compiled and linked and never run. Their parameters stand for what the host owns, so each one
// is a pointer or a reference that the documentation names in its text.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <QWidget>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace qqs = qar_qt_streaming;

std::unique_ptr<qqs::QtPanelStreamManager> snippet_manager_config(
  std::shared_ptr<qar_ar_core::IQarPanelSenderFactory> factory,
  std::shared_ptr<qar_ar_core::IQarPanelInputSource> input,
  qar_ar_core::PanelDiagnosticsSink * sink, qqs::IQarPanelInputSink * input_sink)
{
  // [qar-snippet:manager_config]
  qqs::QtPanelStreamingConfig config;
  config.panel_sender_factory = factory;      // required: the one seam to QarOS
  config.input_source = input;                // optional: the AR pointer samples; drained on every tick
  config.diagnostics = sink;                  // optional: the lines of the manager; without it they are only counted
  config.input_sink = input_sink;             // optional: a coarse hover or click for every resolved interaction
  config.clock = std::make_shared<qqs::SteadyClock>();   // optional: a test passes a clock that it advances by hand
  config.pump_input_in_tick = false;          // a host whose tick is a QTimer calls pump_input() from a posted event
  config.dialog_panels = true;                // a dialog of a streamed panel gets a panel of its own
  config.dialog_panel_offset_m = 0.5;         // metres in X between a panel and each of its dialog panels
  config.unserved_warn_seconds = 5.0;         // report a sender that has served no frame for this long; 0 switches it off
  config.close_mode = qqs::PanelCloseMode::Destroy;   // what a closed window does to its AR panel
  config.max_tombstoned_panels = 16;          // only for the notice mode; 0 means no limit
  config.pointer_state.double_click_interval_s = 0.4;
  config.pointer_state.double_click_distance_px = 5.0;
  // [qar-snippet-end:manager_config]
  return std::make_unique<qqs::QtPanelStreamManager>(config);   // throws std::invalid_argument without a factory
}

void snippet_manager_use(qqs::QtPanelStreamManager * manager, QWidget * widget)
{
  // [qar-snippet:manager_use]
  qqs::PanelStreamOptions options;
  options.display_name = "Settings";                                  // the title that QarOS shows; the key when empty
  options.placement.pose.position = QarVector3{0.0f, 1.5f, -1.0f};    // the top centre of the panel, in room space

  const qar_ar_core::Result added = manager->add_panel("settings", widget, options);
  if (!added.ok()) {
    // InvalidArgument: an empty key or a null widget. AlreadyExists: the key streams already.
    // InvalidState: the cap of the notice mode is reached.
    std::printf("add_panel: %s: %s\n", qar_ar_core::to_string(added.status), added.message.c_str());
    return;
  }

  // Move the panel; its size always comes from the widget.
  QarPose pose = options.placement.pose;
  pose.position.x = 0.5f;
  manager->set_panel_pose("settings", pose);

  const std::uint32_t submitted = manager->tick();       // one capture and submit cycle for every panel
  const std::uint32_t samples = manager->pump_input();   // from a posted event when pump_input_in_tick is false
  std::printf("%u frames submitted, %u pointer samples\n", static_cast<unsigned>(submitted), static_cast<unsigned>(samples));

  manager->close_panel("settings", "the window was closed");   // the one door of every close on the Qt side
  // [qar-snippet-end:manager_use]
}

// [qar-snippet:input_sink]
// A sink that prints what the manager resolved. It is optional: the manager injects every action into the widgets
// whether or not a sink is set.
class PrintingSink : public qqs::IQarPanelInputSink
{
public:
  void on_panel_pointer(const qqs::PanelPointerEvent & event) override
  {
    std::printf("%s: %s at widget pixel (%d, %d), u %.2f, v %.2f\n", event.panel_key.c_str(),
      event.kind == qqs::PanelPointerEvent::Kind::Click ? "click" : "hover", event.widget_x, event.widget_y,
      static_cast<double>(event.hit.u), static_cast<double>(event.hit.v));
  }
};
// [qar-snippet-end:input_sink]

void snippet_diagnostics(qqs::QtPanelStreamManager * manager)
{
  // [qar-snippet:diagnostics]
  const qar_ar_core::PanelDiagnosticsSnapshot snapshot = manager->diagnostics();   // a copy, on the GUI thread
  std::printf("%u panels, %u live, %llu closed\n", static_cast<unsigned>(snapshot.panel_count),
    static_cast<unsigned>(snapshot.live_panels), static_cast<unsigned long long>(snapshot.panels_closed));
  for (const qar_ar_core::PanelStreamDiagnostics & panel : snapshot.panels) {
    std::printf("%s: %llu submitted, %llu unchanged, %llu not ready, %llu rebuilds, visible %d\n", panel.key.c_str(),
      static_cast<unsigned long long>(panel.frames_submitted),
      static_cast<unsigned long long>(panel.frames_skipped_unchanged),
      static_cast<unsigned long long>(panel.frames_skipped_not_ready),
      static_cast<unsigned long long>(panel.adapter_rebuilds), panel.widget_visible ? 1 : 0);
  }
  manager->reset_submit_gap_windows();   // start a new window for the submit_gap statistics
  // [qar-snippet-end:diagnostics]
}

// Referenced by snippets_main.cpp, so that the sink is compiled and linked as well.
void snippet_use_sink(qqs::QtPanelStreamManager * manager)
{
  PrintingSink sink;
  qqs::QtPanelStreamingConfig config;
  config.input_sink = &sink;
  (void)manager;
  (void)config;
}
