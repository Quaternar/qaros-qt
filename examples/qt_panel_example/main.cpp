// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Streams two Qt widgets to QAROS as AR GUI panels: a window with a button and a counter, and a
// panel that is shown off-screen and never reaches the desktop. The program joins the QAROS Hub
// before it shows any window. On the PC that runs the Hub, QAROS asks once to allow the program
// (and again after each rebuild of it); a Hub on another PC is joined with a pairing code.

// [qar-snippet:includes]
// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which Qt's emit macro breaks.
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "qar_ar_core/common_name.hpp"
#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_panel_sender_adapter.hpp"
#include "qar_qar_adapter/qar_session_owner.hpp"
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

namespace
{

constexpr const char * kProgramTag = "qt-panel-example";

void print_usage()
{
  std::printf(
    "Usage: qar_qt_panel_example [--local | --hub HOST:PORT [--code CODE] | --forget]\n"
    "                            [--connect-only] [--seconds N]\n"
    "  --local         join the QAROS Hub of this PC (the default). QAROS asks once to allow\n"
    "                  this program, and again after each rebuild of it: click Allow in the\n"
    "                  QAROS window\n"
    "  --hub H:P       join the Hub of another PC at host H and onboarding port P (the port is\n"
    "                  shown with the pairing code in the Visualizer of that PC)\n"
    "  --code CODE     the pairing code that Hub shows; it works once and lives about 10 seconds.\n"
    "                  Only the first join needs it: the pairing is stored for the next start\n"
    "  --forget        erase the stored pairing with another PC's Hub and exit\n"
    "  --connect-only  join QAROS, print the runtime, the mode, this program's peer and the\n"
    "                  targets, and exit; no window, no panel\n"
    "  --seconds N     otherwise: show two panels, stop after N s (default 60)\n");
}

// "HOST:PORT" with a port from 1 to 65535.
bool parse_hub(const std::string & text, qqa::HubEndpoint & hub)
{
  const std::size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) { return false; }
  const long port = std::strtol(text.c_str() + colon + 1, nullptr, 10);
  if (port < 1 || port > 65535) { return false; }
  hub.host = text.substr(0, colon);
  hub.port = static_cast<std::uint16_t>(port);
  return true;
}

// Prints what the Hub-facing events say, the plain-words status first.
void print_events(const std::vector<qqa::SessionEvent> & events)
{
  for (const qqa::SessionEvent & event : events) {
    if (event.kind == qqa::SessionEvent::Kind::kState && !event.status.message.empty()) {
      std::printf("example: QAROS: %s\n", event.status.message.c_str());
    } else if (event.kind == qqa::SessionEvent::Kind::kProgress && !event.text.empty()) {
      std::printf("example: QAROS: %s\n", event.text.c_str());
    }
  }
}

std::string peer_text(const QarPeerId & id)
{
  char buffer[16] = {0};
  std::snprintf(
    buffer, sizeof(buffer), "%02x%02x%02x%02x", id.data[0], id.data[1], id.data[2], id.data[3]);
  return buffer;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  bool connect_only = false;
  bool forget = false;
  bool local = false;
  std::string hub_text;
  std::string code;
  int seconds = 60;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  bool bad_usage = false;
  for (int i = 1; i < argc && !help && !bad_usage; ++i) {
    const std::string arg = argv[i];
    if (arg == "--connect-only") { connect_only = true; }
    else if (arg == "--local") { local = true; }
    else if (arg == "--forget") { forget = true; }
    else if (arg == "--hub" && i + 1 < argc) { hub_text = argv[++i]; }
    else if (arg == "--code" && i + 1 < argc) { code = argv[++i]; }
    else if (arg == "--seconds" && i + 1 < argc) { seconds = std::atoi(argv[++i]); }
    else { bad_usage = true; }
  }
  qqa::HubEndpoint hub;
  if (!help && !bad_usage && !hub_text.empty() && !parse_hub(hub_text, hub)) {
    std::printf("example: --hub needs HOST:PORT with a port from 1 to 65535\n");
    bad_usage = true;
  }
  const bool conflicting = local && (!hub_text.empty() || forget);
  if (!help && !bad_usage && (conflicting || (!code.empty() && hub_text.empty()) || seconds < 1)) {
    bad_usage = true;
  }
  if (help || bad_usage) {
    print_usage();
    return help ? 0 : 2;
  }
  std::unique_ptr<QApplication> app;  // a connection check needs no Qt platform plugin
  if (!connect_only && !forget) { app = std::make_unique<QApplication>(argc, argv); }
  const std::string host = QSysInfo::machineHostName().toStdString();
  // [qar-snippet:session]
  qqa::SessionOwnerConfig owner_config;
  owner_config.program_tag = kProgramTag;
  owner_config.display_name = std::string("Qt panel example on ") + host;
  owner_config.mode = (!hub_text.empty() || forget) ? qqa::HubMode::kRemote : qqa::HubMode::kLocal;
  owner_config.remote = hub;
  owner_config.log_severity = QAR_LOG_SEVERITY_WARN;
  owner_config.enable_console_logging = false;
  auto owner = std::make_shared<qqa::QarSessionOwner>(owner_config);  // starts its own thread
  // [qar-snippet-end:session]
  if (forget) {
    owner->forget();
    for (int i = 0; i < 750; ++i) {  // 15 s
      std::vector<qqa::SessionEvent> events;
      owner->poll(events);
      for (const qqa::SessionEvent & event : events) {
        if (event.kind == qqa::SessionEvent::Kind::kForgetDone) {
          std::printf("example: %s\n", event.status.message.c_str());
          return 0;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("example: no answer from QAROS to --forget\n");
    return 1;
  }
  if (owner_config.mode == qqa::HubMode::kLocal) {
    std::printf("example: joining the QAROS Hub of this PC; click Allow if QAROS asks\n");
  } else {
    std::printf("example: joining the QAROS Hub at %s:%u\n", hub.host.c_str(),
      static_cast<unsigned>(hub.port));
  }
  owner->connect();
  std::vector<qqa::SessionEvent> seen;
  // The approval prompt waits 120 s by default; the wait here is a little longer.
  qqa::SessionState state = qqa::wait_for_session(*owner, std::chrono::seconds(150), &seen);
  if (state == qqa::SessionState::kNeedsCode && !code.empty()) {
    owner->submit_code(code, hub);
    code.assign(code.size(), '\0');  // the code works once; do not keep it around
    state = qqa::wait_for_session(*owner, std::chrono::seconds(60), &seen);
  }
  print_events(seen);
  if (state == qqa::SessionState::kNeedsCode) {
    std::printf("example: not paired with that Hub; run with --hub HOST:PORT --code CODE\n");
    return 1;
  }
  if (state != qqa::SessionState::kRunning) {
    std::printf("example: QAROS connect failed (state %d)\n", static_cast<int>(state));
    return 1;
  }
  std::printf("example: connected to QarOS: ready=1\n");
  if (connect_only) {
    // The peer list arrives right after the join; give it a moment before it is printed.
    std::vector<qqa::SessionEvent> late;
    for (int i = 0; i < 75; ++i) {  // 1.5 s
      owner->poll(late);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("example: runtime: %s\n", owner->runtime_description().c_str());
    const bool is_local = owner->effective_mode() == qqa::HubMode::kLocal;
    std::printf("example: mode: %s\n", is_local ? "local" : "remote");
    std::printf("example: this program is peer %s\n", peer_text(owner->self_peer()).c_str());
    const std::vector<qqa::TargetInfo> targets = owner->targets();
    std::printf("example: %u target(s)\n", static_cast<unsigned>(targets.size()));
    for (const qqa::TargetInfo & target : targets) {
      std::printf("example:   target %s '%s'%s\n", peer_text(target.id).c_str(),
        target.device_name.empty() ? target.display_name.c_str() : target.device_name.c_str(),
        target.served ? "" : " (not served)");
    }
    return 0;
  }
  // [qar-snippet:panel_factory]
  auto input = std::make_shared<qqa::QarCApiPanelInputSource>(owner->session());
  auto panel_factory = std::make_shared<qqa::QarCApiPanelSenderFactory>(owner.get(), input);
  // [qar-snippet-end:panel_factory]
  // [qar-snippet:manager_config]
  PrintSink sink;
  qqs::QtPanelStreamingConfig streaming;
  streaming.panel_sender_factory = panel_factory;
  streaming.input_source = input;
  streaming.pump_input_in_tick = false;  // the timer below posts the input pump instead
  streaming.diagnostics = &sink;
  // Panels are hidden, never closed, when the program ends: the next start finds them by name.
  const auto close_choice =
    qqs::parse_panel_close_mode(qgetenv(qqs::kPanelCloseModeEnv).constData());
  streaming.close_mode =
    close_choice.explicitly_chosen ? close_choice.mode : qqs::PanelCloseMode::Park;
  streaming.name_scope = qar_ar_core::make_name_scope(kProgramTag, "", host);
  streaming.resize_mode =
    qqs::parse_panel_resize_mode(qgetenv(qqs::kPanelResizeModeEnv).constData()).mode;
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
    std::vector<qqa::SessionEvent> events;  // the owner's news: a lost Hub, a target that went
    owner->poll(events);
    print_events(events);
    // [qar-snippet:close]
    if (!window.isVisible() && manager->has_panel("counter") && !manager->is_tombstoned("counter")) {
      manager->close_panel("counter", "closed on the desktop");  // hides its AR panel
    }
    // [qar-snippet-end:close]
  });
  timer.start(16);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, app.get(), &QCoreApplication::quit);
  deadline.start(seconds * 1000);
  app->exec();
  // [qar-snippet-end:tick]
  // [qar-snippet:shutdown]
  manager.reset();  // hides the panels that are still open and lets go of their video senders;
                    // the factories and the session owner follow when main ends, in the reverse
                    // order of their declarations
  // [qar-snippet-end:shutdown]
  std::printf("example: done\n");
  return 0;
}
