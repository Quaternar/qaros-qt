// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Streams two Qt widgets to QAROS as AR GUI panels: a window with a button and a counter, and a
// panel that is shown off-screen and never reaches the desktop. The program joins the QAROS Hub
// before it shows any window. On the PC that runs the Hub, QAROS asks once to allow the program
// (and again after each rebuild of it). The program joins as a local app
// (OnboardBuilder::WithLocalApp, QarOnboardLocalAppExt in the C API), the way QAROS recommends; it
// uses no deprecated join (no pairing code, no rejoin, no forget).
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
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
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
    "Usage: qar_qt_panel_example [--local] [--invite CONNECTION] [--connect-only] [--seconds N]\n"
    "  --local         join the QAROS Hub of this PC as a local app (the only mode, and the\n"
    "                  default). QAROS asks once to allow this program, and again after each\n"
    "                  rebuild of it: click Allow in the QAROS window\n"
    "  --invite CONN   ask QAROS to start the target app named by the connection string CONN\n"
    "  --connect-only  join QAROS, print the session, and exit; no window, no panel\n"
    "  --seconds N     otherwise: show two panels, stop after N s (default 60)\n"
    "Ctrl+C in this console ends the program the orderly way, also while the join waits.\n");
}

struct Options
{
  bool connect_only = false;
  std::string invite;
  int seconds = 60;
};

// The folder of this program's QAROS identities, below the local application data of the Windows
// account and apart from the data of the Hub.
QString identity_folder()
{
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty()) { base = QDir::homePath() + "/AppData/Local"; }
  const QString path = QDir(base).filePath(QString("Quaternar/QarOS-Qt/") + kProgramTag);
  QDir().mkpath(path);
  return path;
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
  // The join of a source app on the PC of the Hub. It blocks until the Hub has answered, which
  // includes the time the person at the PC takes to allow the program on its first start (the Hub
  // waits 120 s by default). The mode is stateless: nothing is stored, and the Hub recognises the
  // program on its next start while it matches what was allowed. The pairing code, the rejoin with
  // a stored onboarding id and its erasure are deprecated by QAROS, so the example does not offer
  // them.
  qaros::OnboardBuilder builder;
  builder.WithLocalApp().WithDisplayName(plan.display_name);
  qaros::Result<qaros::OnboardSession> joined = out.runtime.Onboard(builder, call);
  if (!joined.Ok()) { out.error = failure("local onboarding", joined.GetStatus()); return out; }
  out.session = std::move(joined).Value().Session;
  out.description = "joined the Hub of this PC";

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
    else if (arg == "--local") {}  // the only mode; accepted so that existing commands keep working
    else if (arg == "--invite" && i + 1 < argc) { options.invite = argv[++i]; }
    else if (arg == "--seconds" && i + 1 < argc) { options.seconds = std::atoi(argv[++i]); }
    else { bad_usage = true; }
  }
  if (!help && !bad_usage && options.seconds < 1) { bad_usage = true; }
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
  plan.display_name = std::string("Qt panel example on ") + host;

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
