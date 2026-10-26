// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Streams two Qt widgets to a QarOS Visualizer as AR GUI panels: a window with a button and a counter, and a
// panel that is shown off-screen and never reaches the desktop. The QarOS session opens before any window.

// [qar-snippet:includes]
// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which Qt's emit macro breaks.
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_panel_sender_adapter.hpp"
#include "qar_qar_adapter/qar_capi_render_sender_adapter.hpp"
// [qar-snippet-end:includes]

namespace qqs = qar_qt_streaming;
namespace qqa = qar_qar_adapter;
// [qar-snippet:sink]
struct PrintSink : qar_ar_core::PanelDiagnosticsSink  // prints what the library reports about its panels
{
  void on_message(qar_ar_core::DiagnosticSeverity, const std::string &, const std::string & message,
    const std::string & key) override { std::printf("example: [%s] %s\n", key.c_str(), message.c_str()); }
};
// [qar-snippet-end:sink]

// [qar-snippet:add_panel]
// The pose is the top centre of the panel in room space; the size of the panel comes from the widget.
bool stream(qqs::QtPanelStreamManager & manager, const std::string & key, QWidget & widget, float x)
{
  qqs::PanelStreamOptions options;
  options.placement.pose.position = QarVector3{x, 1.5f, -1.0f};
  const qar_ar_core::Result added = manager.add_panel(key, &widget, options);
  if (!added.ok()) { std::printf("example: could not stream %s: %s\n", key.c_str(), added.message.c_str()); }
  return added.ok();
}
// [qar-snippet-end:add_panel]

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string sdk_bin;
  bool connect_only = false;
  int seconds = 60;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  for (int i = 1; i < argc && !help; ++i) {
    const std::string arg = argv[i];
    if (arg == "--connect-only") { connect_only = true; }
    else if (arg == "--sdk-bin" && i + 1 < argc) { sdk_bin = argv[++i]; }
    else if (arg == "--seconds" && i + 1 < argc) { seconds = std::atoi(argv[++i]); }
    else { sdk_bin.clear(); break; }
  }
  if (help || sdk_bin.empty() || seconds < 1) {
    std::printf("Usage: qar_qt_panel_example --sdk-bin DIR [--connect-only] [--seconds N]\n"
      "  --sdk-bin DIR   bin folder of the QarOS SDK (required)\n"
      "  --connect-only  open a QarOS session and exit; no window, no peer\n"
      "  --seconds N     otherwise: invite a Visualizer (it opens a window), show two panels, stop after N s (default 60)\n");
    return help ? 0 : 2;
  }
  std::unique_ptr<QApplication> app;  // a connection check needs no Qt platform plugin
  if (!connect_only) { app = std::make_unique<QApplication>(argc, argv); }
  // [qar-snippet:session]
  qqa::QarCApiSenderFactory::Config config;
  config.runtime_binaries_folder_path = sdk_bin;  // QarOS starts its launcher and Visualizer from here
  config.log_severity = QAR_LOG_SEVERITY_WARN;
  config.enable_console_logging = false;
  auto session = std::make_shared<qqa::QarCApiSenderFactory>(config);  // blocks until the session is up
  if (!session->ready()) { std::printf("example: QarOS connect failed: %s\n", session->last_error().c_str()); return 1; }
  std::printf("example: connected to QarOS: ready=1\n");
  // [qar-snippet-end:session]
  if (connect_only) { return 0; }
  QarPeerId visualizer{};
  if (const std::string failed = session->invite_additional_visualizer(visualizer); !failed.empty()) {
    std::printf("example: could not invite a Visualizer: %s\n", failed.c_str());
    return 1;
  }
  // [qar-snippet:panel_factory]
  auto input = std::make_shared<qqa::QarCApiPanelInputSource>(session->session());
  auto panel_factory = std::make_shared<qqa::QarCApiPanelSenderFactory>(session.get(), input);
  // [qar-snippet-end:panel_factory]
  // [qar-snippet:manager_config]
  PrintSink sink;
  qqs::QtPanelStreamingConfig streaming;
  streaming.panel_sender_factory = panel_factory;
  streaming.input_source = input;
  streaming.pump_input_in_tick = false;  // the timer below posts the input pump instead
  streaming.diagnostics = &sink;
  streaming.close_mode = qqs::parse_panel_close_mode(qgetenv(qqs::kPanelCloseModeEnv).constData()).mode;
  auto manager = std::make_unique<qqs::QtPanelStreamManager>(streaming);
  // [qar-snippet-end:manager_config]
  // [qar-snippet:visible_panel]
  QWidget window;
  QVBoxLayout layout(&window);
  QLabel label("clicked 0 times");
  QPushButton button("Tap me");
  layout.addWidget(&label);  layout.addWidget(&button);
  int clicks = 0;
  QObject::connect(&button, &QPushButton::clicked, [&] {
    label.setText(QString("clicked %1 times").arg(++clicks));
    std::printf("example: clicked %d\n", clicks);
  });
  window.resize(420, 200);  // 0.50 m wide in AR
  window.show();
  if (!stream(*manager, "counter", window, 0.0f)) { return 1; }
  // [qar-snippet-end:visible_panel]
  // [qar-snippet:hidden_panel]
  QWidget hidden;
  QComboBox choice(&hidden);
  choice.addItems({"first", "second", "third"});
  choice.setGeometry(10, 10, 340, 40);  // a target that is large enough for a tap in AR
  qqs::show_offscreen(&hidden, 360, 240);  // shown to Qt, never to the desktop
  if (!stream(*manager, "hidden", hidden, -0.7f)) { return 1; }
  // [qar-snippet-end:hidden_panel]
  // [qar-snippet:tick]
  app->setQuitOnLastWindowClosed(false);  // the example ends when its time is up, not with its window
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    // A posted event, never this handler: a modal dialog would stall the timer that runs the tick.
    QMetaObject::invokeMethod(app.get(), [&] { manager->pump_input(); }, Qt::QueuedConnection);
    manager->tick();
    // [qar-snippet:close]
    if (!window.isVisible() && manager->has_panel("counter") && !manager->is_tombstoned("counter")) {
      manager->close_panel("counter", "closed on the desktop");  // a closed window closes its AR panel
    }
    // [qar-snippet-end:close]
  });
  timer.start(16);
  QTimer::singleShot(seconds * 1000, app.get(), &QCoreApplication::quit);
  app->exec();
  // [qar-snippet-end:tick]
  // [qar-snippet:shutdown]
  manager.reset();  // closes the panels that are still open and destroys their video senders; the factories
                    // and the session follow when main ends, in the reverse order of their declarations
  // [qar-snippet-end:shutdown]
  std::printf("example: done\n");
  return 0;
}
