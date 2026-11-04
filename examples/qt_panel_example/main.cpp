// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Streams two Qt widgets to QAROS as AR GUI panels: a window with a button and a counter, and a
// panel that is shown off-screen and never reaches the desktop. The program joins the QAROS Hub
// before it shows any window. On the PC that runs the Hub, QAROS asks once to allow the program
// (and again after each rebuild of it); a Hub on another PC is joined with a pairing code, and the
// pairing is stored for the next start.
//
// Every call that can wait on QAROS runs off the Qt thread: the join on a thread of its own, the
// panels' calls on the worker of the backend. The Qt thread polls the join and runs the tick.
// Ctrl+C in the console ends the program the orderly way, also while the join still waits. A call
// into QAROS that does not return is never waited for without a bound at the end of the program.

// [qar-snippet:includes]
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_input_source.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_sender_factory.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/seam/common_name.hpp"
#include "qar_qt_streaming/seam/timing_log.hpp"

#include <qar_streaming_cpp/Library.hpp>
#include <qar_streaming_cpp/Runtime.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "local_app_onboarding.hpp"
// [qar-snippet-end:includes]

// The program expands the function tables of the QAROS C header, once. The library that makes
// the panels (qar_qt_streaming::qaros) expands none.
QAR_IMPLEMENT_DYNAMIC_LOADING()

namespace qqs = qar_qt_streaming;
namespace qqb = qar_qt_streaming::qaros_backend;
namespace seam = qar_qt_streaming::seam;

// [qar-snippet:sink]
struct PrintSink : seam::PanelDiagnosticsSink  // prints what the library reports about its panels
{
  void on_message(seam::DiagnosticSeverity, const std::string &, const std::string & message,
    const std::string & key) override
  { std::printf("example: [%s] %s\n", key.c_str(), message.c_str()); }
};
// [qar-snippet-end:sink]

// [qar-snippet:add_panel]
// The pose is the top centre of the panel in room space; the size of the panel comes from the
// widget.
bool stream(qqs::QtPanelStreamManager & manager, const std::string & key, QWidget & widget, float x)
{
  qqs::PanelStreamOptions options;
  options.placement.pose.position = seam::Vector3{x, 1.5f, -1.0f};
  const seam::Result added = manager.add_panel(key, &widget, options);
  if (!added.ok()) {
    std::printf("example: could not stream %s: %s\n", key.c_str(), added.message.c_str());
  }
  return added.ok();
}
// [qar-snippet-end:add_panel]

namespace
{

constexpr const char * kProgramTag = "qt-panel-example";

// How long the end of the program waits for the worker of the backend to run its queued calls
// (the factory abandons a worker that is still busy after it), and how long it waits for a join
// that was asked to stop. After either wait ran out the program ends without releasing QAROS.
constexpr std::chrono::milliseconds kShutdownBudget{8000};
constexpr std::chrono::milliseconds kJoinGrace{5000};

// Set by Ctrl+C in the console; the timer of the Qt thread polls it. A signal handler does
// nothing else.
std::atomic<bool> g_stop_requested{false};

void on_interrupt(int) { g_stop_requested.store(true); }

void print_usage()
{
  std::printf(
    "Usage: qar_qt_panel_example [--local | --rejoin | --hub HOST:PORT [--code CODE]]\n"
    "                            [--forget] [--invite CONNECTION] [--connect-only] [--seconds N]\n"
    "  --local         join the QAROS Hub of this PC (the default when no pairing is stored).\n"
    "                  QAROS asks once to allow this program, and again after each rebuild of\n"
    "                  it: click Allow in the QAROS window\n"
    "  --rejoin        join again with the pairing that an earlier --hub --code run stored (the\n"
    "                  default when one is stored)\n"
    "  --hub H:P       join the Hub of another PC at host H and port P (shown with the pairing\n"
    "                  code in the Visualizer of that PC). Without --code it joins again with\n"
    "                  the stored pairing, which names its own Hub: H:P is then not used, and\n"
    "                  without a stored pairing the program stops and asks for --code\n"
    "  --code CODE     the pairing code that Hub shows. Only the first join needs it: the\n"
    "                  pairing is stored for the next start\n"
    "  --forget        erase the stored pairing and exit\n"
    "  --invite CONN   ask QAROS to start the target app named by the connection string CONN\n"
    "  --connect-only  join QAROS, print the session, and exit; no window, no panel\n"
    "  --seconds N     otherwise: show two panels, stop after N s (default 60)\n"
    "Ctrl+C in this console ends the program the orderly way, also while the join waits.\n");
}

struct HubEndpoint
{
  std::string host;
  std::uint16_t port = 0;
};

// "HOST:PORT" with a port from 1 to 65535.
bool parse_hub(const std::string & text, HubEndpoint & hub)
{
  const std::size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) { return false; }
  const long port = std::strtol(text.c_str() + colon + 1, nullptr, 10);
  if (port < 1 || port > 65535) { return false; }
  hub.host = text.substr(0, colon);
  hub.port = static_cast<std::uint16_t>(port);
  return true;
}

struct Options
{
  bool connect_only = false;
  bool forget = false;
  bool local = false;
  bool rejoin = false;
  std::string hub_text;
  std::string code;
  std::string invite;
  int seconds = 60;
};

// The folder of this program's QAROS identities and of the stored pairing, below the local
// application data of the Windows account and apart from the data of the Hub.
QString identity_folder()
{
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty()) { base = QDir::homePath() + "/AppData/Local"; }
  const QString path = QDir(base).filePath(QString("Quaternar/QarOS-Qt/") + kProgramTag);
  QDir().mkpath(path);
  return path;
}

QString stored_id_path() { return QDir(identity_folder()).filePath("onboarding_id.txt"); }

std::optional<qaros::OnboardingId> read_stored_id()
{
  QFile file(stored_id_path());
  if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
  const std::string text = QString::fromUtf8(file.readAll()).trimmed().toStdString();
  const qaros::Result<qaros::OnboardingId> id = qaros::OnboardingId::FromString(text);
  if (!id.Ok() || id.Value().IsZero()) { return std::nullopt; }
  return id.Value();
}

void write_stored_id(const qaros::OnboardingId & id)
{
  const qaros::Result<std::string> text = id.ToString();
  QFile file(stored_id_path());
  if (text.Ok() && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(QByteArray::fromStdString(text.Value()));
  } else {
    std::printf("example: the pairing could not be stored\n");
  }
}

// What the join leaves behind. The members go in the reverse order of their declaration: the
// session first, the library last.
struct Joined
{
  qaros::Library library;
  qaros::Runtime runtime;
  qaros::Session session;
  std::string error;
  std::string description;
};

// The cancel token of a join in progress, so that quitting can ask the join to stop. The token is
// made on the join thread, after the library is up, so that no QAROS call runs on the Qt thread
// before the join. A cancel that arrives before the token exists is kept: the join thread sees it
// when it makes the token and does not start the wait.
struct JoinControl
{
  std::mutex mutex;
  qaros::CancelToken token;
  bool cancelled = false;

  void cancel()
  {
    std::lock_guard<std::mutex> lock(mutex);
    cancelled = true;
    if (token) { (void)token.Cancel(); }
  }
};

struct JoinPlan
{
  Options options;
  HubEndpoint hub;
  std::optional<qaros::OnboardingId> stored;
  std::string display_name;
};

std::string failure(const char * what, const qaros::Status & status)
{
  return std::string(what) + " failed: " + status.Message();
}

// [qar-snippet:session]
// Starts QAROS and joins the session. May take as long as the person at the PC takes to allow the
// program, so it never runs on the Qt thread.
Joined join_qaros(const JoinPlan & plan, JoinControl & control)
{
  Joined out;
  qaros::Result<qaros::Library> library =
    qaros::Library::Init(qaros::LibraryBuilder().WithConsoleLogging(false));
  if (!library.Ok()) { out.error = failure("Library::Init", library.GetStatus()); return out; }
  out.library = std::move(library).Value();

  qaros::RuntimeBuilder runtime_builder;
  runtime_builder.WithStorageFolder(identity_folder().toStdString());  // never the Hub's own data
  qaros::Result<qaros::Runtime> runtime = qaros::Runtime::Create(runtime_builder);
  if (!runtime.Ok()) { out.error = failure("Runtime::Create", runtime.GetStatus()); return out; }
  out.runtime = std::move(runtime).Value();

  qaros::CallOptions call;
  {
    std::lock_guard<std::mutex> lock(control.mutex);
    if (control.cancelled) { out.error = "the join was cancelled"; return out; }
    qaros::Result<qaros::CancelToken> token = qaros::CancelToken::Create();
    if (token.Ok()) { control.token = std::move(token).Value(); }
    call.Cancel = control.token.Get();
  }

  const Options & options = plan.options;
  if (!options.hub_text.empty() && !options.code.empty()) {
    qaros::OnboardBuilder builder;
    builder.WithCode(options.code, plan.hub.host, plan.hub.port).WithDisplayName(plan.display_name);
    qaros::Result<qaros::OnboardSession> joined = out.runtime.Onboard(builder, call);
    if (!joined.Ok()) { out.error = failure("Onboard", joined.GetStatus()); return out; }
    write_stored_id(joined.Value().OnboardingId);  // the ticket for every later start
    out.session = joined.Value().Session;
    out.description = "joined the Hub at " + plan.hub.host + " and stored the pairing";
  } else if (plan.stored && !options.local) {
    qaros::RejoinBuilder builder(*plan.stored);
    builder.WithDisplayName(plan.display_name);
    qaros::Result<qaros::Session> joined = out.runtime.Rejoin(builder, call);
    if (!joined.Ok()) { out.error = failure("Rejoin", joined.GetStatus()); return out; }
    out.session = std::move(joined).Value();
    out.description = "joined again with the stored pairing";
  } else if (!options.hub_text.empty() || options.rejoin) {
    out.error = options.rejoin ? "no pairing is stored; run with --hub HOST:PORT --code CODE"
                               : "--hub needs --code for the first join";
    return out;
  } else {
    qaros::Result<qaros::Session> joined =
      onboard_local_app(out.runtime, call.Cancel, plan.display_name);
    if (!joined.Ok()) { out.error = failure("local onboarding", joined.GetStatus()); return out; }
    out.session = std::move(joined).Value();
    out.description = "joined the Hub of this PC";
  }

  if (!options.invite.empty()) {
    const qaros::Result<qaros::PeerId> invited = out.session.InviteTargetApp(options.invite, call);
    if (!invited.Ok()) {
      std::printf("example: the target app could not be invited: %s\n",
        invited.GetStatus().Message().c_str());
    }
  }
  return out;
}
// [qar-snippet-end:session]

}  // namespace

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Options options;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  bool bad_usage = false;
  for (int i = 1; i < argc && !help && !bad_usage; ++i) {
    const std::string arg = argv[i];
    if (arg == "--connect-only") { options.connect_only = true; }
    else if (arg == "--local") { options.local = true; }
    else if (arg == "--rejoin") { options.rejoin = true; }
    else if (arg == "--forget") { options.forget = true; }
    else if (arg == "--hub" && i + 1 < argc) { options.hub_text = argv[++i]; }
    else if (arg == "--code" && i + 1 < argc) { options.code = argv[++i]; }
    else if (arg == "--invite" && i + 1 < argc) { options.invite = argv[++i]; }
    else if (arg == "--seconds" && i + 1 < argc) { options.seconds = std::atoi(argv[++i]); }
    else { bad_usage = true; }
  }
  HubEndpoint hub;
  if (!help && !bad_usage && !options.hub_text.empty() && !parse_hub(options.hub_text, hub)) {
    std::printf("example: --hub needs HOST:PORT with a port from 1 to 65535\n");
    bad_usage = true;
  }
  const bool conflicting = (options.local && (!options.hub_text.empty() || options.rejoin)) ||
    (options.rejoin && !options.hub_text.empty()) ||
    (options.forget && (options.local || !options.code.empty()));
  if (!help && !bad_usage &&
    (conflicting || (!options.code.empty() && options.hub_text.empty()) || options.seconds < 1))
  {
    bad_usage = true;
  }
  if (help || bad_usage) {
    print_usage();
    return help ? 0 : 2;
  }

  // Loads the QAROS runtime library (the lookup of the C header: the folder of this program, then
  // the installed QAROS). Everything else of QAROS is released before it is unloaded.
  if (!qar_library_load(nullptr)) {
    std::printf("example: the QAROS runtime library could not be loaded\n");
    return 1;
  }
  struct Unloader { ~Unloader() { qar_library_unload(); } } unloader;

  const std::string host = QSysInfo::machineHostName().toStdString();
  JoinPlan plan;
  plan.options = options;
  plan.hub = hub;
  plan.stored = read_stored_id();
  plan.display_name = std::string("Qt panel example on ") + host;
  if (options.code.empty() == false && plan.stored) { plan.stored.reset(); }  // a new pairing wins
  if (!options.hub_text.empty() && options.code.empty() && plan.stored) {
    std::printf("example: --hub without --code joins again with the stored pairing; "
      "%s is not used\n", options.hub_text.c_str());
  }

  if (options.forget) {
    if (!plan.stored) {
      std::printf("example: no pairing is stored (the local join stores nothing)\n");
      return 0;
    }
    qaros::Result<qaros::Library> library =
      qaros::Library::Init(qaros::LibraryBuilder().WithConsoleLogging(false));
    qaros::RuntimeBuilder runtime_builder;
    runtime_builder.WithStorageFolder(identity_folder().toStdString());
    qaros::Result<qaros::Runtime> runtime = qaros::Runtime::Create(runtime_builder);
    if (!library.Ok() || !runtime.Ok()) {
      std::printf("example: QAROS could not be started\n");
      return 1;
    }
    const qaros::Result<void> forgotten = runtime.Value().Forget(*plan.stored);
    if (!forgotten.Ok()) {
      std::printf("example: %s\n", failure("Forget", forgotten.GetStatus()).c_str());
      return 1;
    }
    QFile::remove(stored_id_path());
    std::printf("example: the stored pairing is erased\n");
    runtime.Value() = qaros::Runtime();
    (void)library.Value().Shutdown();
    return 0;
  }

  if (options.connect_only) {
    JoinControl control;
    Joined joined = join_qaros(plan, control);  // waits here: no window exists to freeze
    if (!joined.error.empty()) {
      std::printf("example: %s\n", joined.error.c_str());
      return 1;
    }
    std::printf("example: %s\n", joined.description.c_str());
    const qaros::Result<qaros::SessionId> id = joined.session.GetId();
    const qaros::Result<std::string> text = id.Ok() ? id.Value().ToString()
                                                    : qaros::Result<std::string>(id.GetStatus());
    std::printf("example: session %s\n", text.Ok() ? text.Value().c_str() : "(unknown)");
    joined.session = qaros::Session();
    joined.runtime = qaros::Runtime();
    (void)joined.library.Shutdown();
    return 0;
  }

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);  // the example ends when its time is up, not its window
  std::signal(SIGINT, on_interrupt);  // Ctrl+C quits through the event loop, see the tick
  std::printf("example: joining QAROS; click Allow if QAROS asks\n");

  // The join runs on a thread of its own; the timer below polls it and never waits for it.
  auto control = std::make_shared<JoinControl>();
  std::future<Joined> joining = std::async(
    std::launch::async, [plan, control] { return join_qaros(plan, *control); });
  Joined joined;
  int exit_code = 0;

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
  // [qar-snippet-end:visible_panel]
  // [qar-snippet:hidden_panel]
  QWidget hidden;
  QComboBox choice(&hidden);
  choice.addItems({"first", "second", "third"});
  choice.setGeometry(10, 10, 340, 40);  // a target that is large enough for a tap in AR
  // [qar-snippet-end:hidden_panel]

  PrintSink sink;
  std::shared_ptr<qqb::QarosPanelInputSource> input;
  std::shared_ptr<qqb::QarosPanelSenderFactory> panel_factory;
  std::unique_ptr<qqs::QtPanelStreamManager> manager;
  QTimer timer;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, &QCoreApplication::quit);

  // Called on the Qt thread when the join has finished.
  const auto start_streaming = [&]() -> bool {
    std::printf("example: %s\n", joined.description.c_str());
    // [qar-snippet:panel_factory]
    input = std::make_shared<qqb::QarosPanelInputSource>();
    qqb::FactoryOptions factory_options;
    factory_options.diagnostics = &sink;
    factory_options.shutdown_budget = kShutdownBudget;
    panel_factory =
      std::make_shared<qqb::QarosPanelSenderFactory>(joined.session, input, factory_options);
    // [qar-snippet-end:panel_factory]
    // [qar-snippet:manager_config]
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
    streaming.name_scope = seam::make_name_scope(kProgramTag, "", host);
    streaming.resize_mode =
      qqs::parse_panel_resize_mode(qgetenv(qqs::kPanelResizeModeEnv).constData()).mode;
    manager = std::make_unique<qqs::QtPanelStreamManager>(streaming);
    // [qar-snippet-end:manager_config]
    window.show();
    if (!stream(*manager, "counter", window, 0.0f)) { return false; }
    qqs::show_offscreen(&hidden, 360, 240);  // shown to Qt, never to the desktop
    return stream(*manager, "hidden", hidden, -0.7f);
  };

  // [qar-snippet:tick]
  QObject::connect(&timer, &QTimer::timeout, [&] {
    if (g_stop_requested.load()) {
      std::printf("example: Ctrl+C; ending\n");
      timer.stop();
      app.quit();
      return;
    }
    if (!manager) {
      if (!joining.valid() ||
        joining.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      {
        return;  // still waiting for the Hub; nothing blocks here
      }
      joined = joining.get();
      if (!joined.error.empty()) {
        std::printf("example: %s\n", joined.error.c_str());
        exit_code = 1;
        timer.stop();
        app.quit();
        return;
      }
      if (!start_streaming()) {
        exit_code = 1;
        timer.stop();
        app.quit();
        return;
      }
      deadline.start(options.seconds * 1000);
      return;
    }
    // A posted event, never this handler: a modal dialog would stall the timer that runs the tick.
    QMetaObject::invokeMethod(&app, [&] { manager->pump_input(); }, Qt::QueuedConnection);
    manager->tick();
    // [qar-snippet:close]
    if (!window.isVisible() && manager->has_panel("counter") &&
      !manager->is_tombstoned("counter"))
    {
      manager->close_panel("counter", "closed on the desktop");  // hides its AR panel
    }
    // [qar-snippet-end:close]
  });
  timer.start(16);
  app.exec();
  // [qar-snippet-end:tick]
  // [qar-snippet:shutdown]
  if (joining.valid()) {  // quit (Ctrl+C) while the Hub was still being waited for
    control->cancel();
    if (joining.wait_for(kJoinGrace) != std::future_status::ready) {
      // The SDK documents a cancel token for its asynchronous calls only, so the synchronous join
      // may keep waiting (for the person at the Hub, up to its own limit). The program does not.
      // No destructor runs: the join thread still holds the library.
      std::printf("example: the join is still waiting for QAROS; ending without it\n");
      std::_Exit(exit_code);
    }
    joined = joining.get();
  }
  manager.reset();  // hides the panels that are still open and queues the release of their video
                    // senders
  seam::TimingLog::instance().shutdown();  // the library's writer thread ends first
  const bool had_factory = static_cast<bool>(panel_factory);
  const auto drain_start = std::chrono::steady_clock::now();
  panel_factory.reset();  // runs the queued calls and ends its worker within the bounded wait
  // The factory is gone after the reset, and with it the answer of worker_abandoned(). A wait that
  // ran the whole budget is the only way for the worker to be abandoned, so it counts as one: the
  // worker may still be inside QAROS or about to release the session.
  const bool worker_left_behind = had_factory &&
    std::chrono::steady_clock::now() - drain_start + std::chrono::milliseconds(100) >=
    kShutdownBudget;
  if (worker_left_behind) {
    // Releasing the session, the runtime and the library, or unloading the runtime library, under
    // that worker could crash the program at its end. The process ends at once instead; the
    // operating system frees the rest.
    std::printf("example: the backend worker was still busy after %lld ms; ending without "
      "releasing QAROS\n", static_cast<long long>(kShutdownBudget.count()));
    std::_Exit(exit_code);
  }
  input.reset();
  joined.session = qaros::Session();  // the program's own copy goes last, never inside a callback
  joined.runtime = qaros::Runtime();
  (void)joined.library.Shutdown();
  // [qar-snippet-end:shutdown]
  std::printf("example: done\n");
  return exit_code;
}
