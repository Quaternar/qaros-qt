# Getting started

## What you will run

One program of this repository, built together with the library: `qar_qt_panel_example`. It streams two Qt widgets to a QarOS Visualizer as AR GUI panels: a window with a button and a counter label, and a second panel that is shown off-screen and never appears on the desktop.

| Command | What it does | Needs | Opens a window |
|---|---|---|---|
| `qar_qt_panel_example --help` | prints the options and exits | the Qt folder on `PATH` | no |
| `qar_qt_panel_example --connect-only` | joins the QAROS Hub, prints the runtime, the mode, its peer and the targets, and exits | the installed QAROS | no |
| `qar_qt_panel_example` | joins the Hub, shows the counter window, streams it and the hidden panel, and stops after `--seconds` seconds, 60 by default | the installed QAROS, a local desktop session | the counter window; the Visualizer opens its own |

The sections follow that order. The last section lists what to check when you put the library into your own Qt application.

## Before you start

Finish [Installation](INSTALL.md), up to and including the build of this repository. The commands below run from the clone and use only the environment variables of the layout. Go to the folder of the clone first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

The example uses the Qt modules Core, Gui and Widgets, and Windows finds the DLLs of a program through the folders on `PATH`, so the Qt `bin` folder has to be on `PATH`. A program that shows a window also needs the platform plugin of Qt, `qwindows.dll`, in a folder named `platforms`; the variable `QT_QPA_PLATFORM_PLUGIN_PATH` names that folder. The example creates its application object, and so loads the plugin, only after `--help` and `--connect-only` have returned, so those two need no plugin. The QarOS DLL is not found through `PATH`: the example loads it from the installed QAROS.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QAR_QT_ROOT\plugins\platforms"
Get-ChildItem -Path "$env:QAR_QT_ROOT\bin\Qt5Widgets.dll", "$env:QAR_QT_ROOT\plugins\platforms\qwindows.dll" -Name
```
```text
Qt5Widgets.dll
qwindows.dll
```

The program is in the build folder, at `build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe`. Start it with `--help` to see the options. An unknown option prints the same text and exits with 2; `--help` exits with 0.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --help
```
```text
Usage: qar_qt_panel_example [--local | --hub HOST:PORT [--code CODE] | --forget]
                            [--connect-only] [--seconds N]
  --local         join the QAROS Hub of this PC (the default). QAROS asks once to allow
                  this program, and again after each rebuild of it: click Allow in the
                  QAROS window
  --hub H:P       join the Hub of another PC at host H and onboarding port P (the port is
                  shown with the pairing code in the Visualizer of that PC)
  --code CODE     the pairing code that Hub shows; it works once and lives about 10 seconds.
                  Only the first join needs it: the pairing is stored for the next start
  --forget        erase the stored pairing with another PC's Hub and exit
  --connect-only  join QAROS, print the runtime, the mode, this program's peer and the
                  targets, and exit; no window, no panel
  --seconds N     otherwise: show two panels, stop after N s (default 60)
```

`--local`, the default, joins the Hub of this PC; `--hub HOST:PORT` with `--code CODE` pairs with the Hub of another PC, and `--forget` erases a stored pairing.

## Check the connection to QarOS without a window

The example needs no path to QAROS. With `--local`, the default, it joins the QAROS Hub of this PC as a source app: QAROS asks once to allow the program, so click Allow in the QAROS window, and it asks again after each rebuild of the program. With `--hub HOST:PORT --code CODE` it pairs with the Hub of another PC, and the pairing is stored for the next start. With `--connect-only` the example creates no application object and no window; it joins the Hub through a session owner of the component `session` of `qar_ogre_adapter`, prints what it found, and exits. The code of that check is the first step of the example.

```cpp
qqa::SessionOwnerConfig owner_config;
owner_config.program_tag = kProgramTag;
owner_config.display_name = std::string("Qt panel example on ") + host;
owner_config.mode = (!hub_text.empty() || forget) ? qqa::HubMode::kRemote : qqa::HubMode::kLocal;
owner_config.remote = hub;
owner_config.log_severity = QAR_LOG_SEVERITY_WARN;
owner_config.enable_console_logging = false;
auto owner = std::make_shared<qqa::QarSessionOwner>(owner_config);  // starts its own thread
```

Constructing the owner starts a thread of its own and does not wait for the Hub, so a window can be on the screen while the program joins; the example polls the owner until the session runs. The example sets the log severity of the QarOS library to `warn` and turns off the copy of the library log to the console; the file log stays on.

Run it. The first line sets `PATH` as above, so that the program starts. The command starts the Hub of the installed QAROS when it is not running, so it is not run for this document and no output is shown.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --connect-only
```

The session owner loads `qar-streaming-c.dll`, initialises the library, creates the runtime and joins the Hub; the session is ready when the owner reports the state `kRunning`. The owner prints lines that start with `[qar-adapter]` and with `QAROS:` about its own state, and QarOS writes its library log into the folder `quaternar` below your `TEMP` folder. When the program ends, the owner destroys the session, the runtime and the library again.

On success the example prints a line that has this form, then the runtime, the mode, its peer and the targets, and exits with 0.

```text
example: connected to QarOS: ready=1
```

When the session does not run, it prints a line that has this form, with the state of the owner as a number, and exits with 1. A Hub that does not know the program makes it ask for `--hub HOST:PORT --code CODE`.

```text
example: QAROS connect failed
```

## Stream one widget into AR

Without `--connect-only` the example creates the application object, joins the Hub as above with no window on the screen, and builds the manager of the library and the two widgets, shows the counter window on the desktop, shows the second widget to Qt only, and ticks the manager from a timer until the time given by `--seconds` is up. The steps follow, each a region of the source of the example, which is compiled with the repository. A fresh Visualizer has no receiver: add one in the Visualizer with its "Add receiver" button, and the Hub lists a target for it.

**1. The panel factory.** A GUI panel needs a QarOS session and a video sender that the same session owns, so the panel factory borrows the session of the session owner instead of opening a second one. The input source subscribes each new panel to the gestures that QarOS reports on it and cancels the subscription when the panel closes.

```cpp
auto input = std::make_shared<qqa::QarCApiPanelInputSource>(owner->session());
auto panel_factory = std::make_shared<qqa::QarCApiPanelSenderFactory>(owner.get(), input);
```

**2. The manager.** `QtPanelStreamManager` is the coordinator: it owns one stream for each widget. Its configuration needs one thing, the panel factory, which is the only seam to QarOS; the constructor throws `std::invalid_argument` without it. The input source is optional: when it is set, the manager drains it for pointer samples. The diagnostics sink is optional as well; without one the manager keeps its counters and writes no lines, so the example sets a sink that prints each line. The close mode comes from the environment variable `QAR_PANEL_CLOSE_MODE`, parsed by a pure function that takes the raw text: the example uses `park` when the variable is unset or empty, so that nothing is closed while it runs. The name scope makes the common names of the panels, so that a panel that was hidden when the last session ended is found and shown again by the next start, and the resize mode comes from `QAR_PANEL_RESIZE`: by default a size change swaps the video sender of the same panel.

```cpp
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
```

```cpp
struct PrintSink : qar_ar_core::PanelDiagnosticsSink  // prints what the library reports about its panels
{
  void on_message(qar_ar_core::DiagnosticSeverity, const std::string &, const std::string & message,
    const std::string & key) override { std::printf("example: [%s] %s\n", key.c_str(), message.c_str()); }
};
```

The sink prints every line of the manager with the key of the panel and the text of the manager. A line of the example that comes from the manager has this form:

```text
example: [%s] %s
```

The first line of every panel is `added panel stream`.

**3. A window becomes a panel.** `add_panel` takes a key, a widget and the options of the panel. It registers the widget and nothing more: the QarOS panel is created on the first tick that finds the widget laid out, so the call never waits for QarOS. The widget is borrowed and has to outlive its stream. The options choose the pose of the panel; its size is derived from the widget at every tick. The pose is the top centre of the panel in room space, and the example hangs the counter window at x = 0 and the hidden panel at x = -0.7 m, both 1.5 m up and at z = -1.0 m.

```cpp
// The pose is the top centre of the panel in room space; the size of the panel comes from the widget.
bool stream(qqs::QtPanelStreamManager & manager, const std::string & key, QWidget & widget, float x)
{
  qqs::PanelStreamOptions options;
  options.placement.pose.position = QarVector3{x, 1.5f, -1.0f};
  const qar_ar_core::Result added = manager.add_panel(key, &widget, options);
  if (!added.ok()) { std::printf("example: could not stream %s: %s\n", key.c_str(), added.message.c_str()); }
  return added.ok();
}
```

The counter window is an ordinary widget with a label and a button. It is 420 by 200 logical pixels, and the library gives a logical pixel 1.2 mm in the room by default, so the panel is 0.504 m wide, and with the frame that QarOS draws around the picture it is 0.309 m high; a display with a higher pixel density makes the picture sharper and not larger.

```cpp
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
```

**4. The tick.** The timer fires every 16 ms on the GUI thread and calls `tick()`: one capture and submit cycle for every panel, each paced by its own limit of 15 frames a second, so most ticks capture nothing. The input pump is posted instead of being called, for the reason that the section on [your own application](#integrating-into-your-qt-application) gives.

```cpp
app->setQuitOnLastWindowClosed(false);  // the example ends when its time is up, not with its window
QTimer timer;
QObject::connect(&timer, &QTimer::timeout, [&] {
  // A posted event, never this handler: a modal dialog would stall the timer that runs the tick.
  QMetaObject::invokeMethod(app.get(), [&] { manager->pump_input(); }, Qt::QueuedConnection);
  manager->tick();
  std::vector<qqa::SessionEvent> events;  // the owner's news: a lost Hub, a target that went
  owner->poll(events);
  print_events(events);
  if (!window.isVisible() && manager->has_panel("counter") && !manager->is_tombstoned("counter")) {
    manager->close_panel("counter", "closed on the desktop");  // hides its AR panel
  }
});
timer.start(16);
QTimer deadline;
deadline.setSingleShot(true);
QObject::connect(&deadline, &QTimer::timeout, app.get(), &QCoreApplication::quit);
deadline.start(seconds * 1000);
app->exec();
```

Start the example and keep it running for the next three sections; they read its log. The command copies everything that the program prints, its own lines and those of the adapter, into a file in your `TEMP` folder as well. You start the example yourself, and you look at the Visualizer window; nothing in this repository clicks or moves a window.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QAR_QT_ROOT\plugins\platforms"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --seconds 120 2>&1 | Tee-Object -FilePath (Join-Path $env:TEMP 'qar_qt_panel_example.log')
```

The two panels are created on the first ticks. For each of them the adapter prints a line when the QarOS panel exists, with the time of day, the key of the panel, the word `created` or, for a panel that an earlier session left behind and that was found again by its common name, `reattached`, the identifiers that QarOS gave the panel and its stream, and the size of the buffer. The line has this form:

```text
[qar-panel-adapter] %s panel '%s' %s: id=%s stream=%s buffer=%ux%u px
```

A second line says that the runtime has accepted the subscription for AR input, with the size of the content that the positions of the gestures are normalised against. It has this form:

```text
[qar-panel-adapter] %s panel '%s' subscribed for AR click+hover: id='%s' content=%ux%u px
```

The manager adds a line with the geometry of the panel whenever it has built a sender, that is at the creation of the panel and after a resize that the library adopted. The line begins with the key of the panel and goes on with the logical size of the widget, the pixel ratio of the display and the size of the buffer in pixels and of the panel in metres; the content scale that it reports is always 1.0, because QarOS reads it as a zoom and not as a resolution.

The example stops by itself when the time is up. It then ends the session: in the mode `park`, which the example uses, the manager hides the panels that are still open and detaches their video senders instead of closing them, which the last section describes, and the example prints a last line that says that the example is done.

## Use AR input on the panel

QarOS reports the gestures on a panel through one subscription for each panel: hover, tap, long press and drag. The callback of QarOS runs on a thread of the library, so the input source only queues the samples, and the example drains the queue on the GUI thread. The manager turns each sample into actions with its pointer state machine and the injector delivers them to the widget as Qt events.

The injector offers each pointer sequence as touch events first and falls back to mouse events for the rest of the sequence when no widget of the chain accepts the touch. That is the default because the standard Qt controls do not accept touch events: a touch alone would press nothing. In the counter window a tap on the button therefore arrives as a mouse press and release, the button emits `clicked`, the example increments its counter, updates the label and prints a line that has this form:

```text
example: clicked %d
```

Tap the button in the Visualizer a few times. Each tap changes the text of the label, the next capture finds the picture changed and sends it, and the example prints one line for each tap. A hover over the panel produces the enter and leave events of Qt, so `underMouse()` and the hover state of the style are right.

When you have stopped the example, list the lines of the tapping from its log.

```powershell
Select-String -Path (Join-Path $env:TEMP 'qar_qt_panel_example.log') -Pattern 'example: clicked' | ForEach-Object { $_.Line }
```

The library keeps its own implicit grab for each pointer: a pointer that pressed a panel stays with it while it moves, so a release outside the panel is not a click. It recognises two quick taps in place as a double click, and it treats the two hands of a user as two pointers, each with its own grab. A press that persists across moves is a drag, which is how a slider handle or a scrollbar thumb follows the pointer; the counter window has no such control. The unit tests drive these cases with synthetic pointer samples; no live session has exercised AR drags on a Qt panel yet, and [What the live sessions showed](VERIFICATION.md#what-the-live-sessions-showed) says what the live sessions showed of the library.

## The hidden off-screen panel idiom

The second widget of the example is a window that Qt treats as shown but that never reaches the desktop. Streaming needs the widget to be visible in Qt's sense, because `QWidget::render()` paints only the children that are visible and the injector finds its target with `childAt()`, which skips hidden children, and showing a top-level widget normally puts a window on the desktop. `show_offscreen(widget, width, height)` shows the widget without that:

- it sets `Qt::WA_DontShowOnScreen`, so that Qt lays the widget out, paints it, delivers events to it and positions its popups against it, and no platform window is made visible;
- it clears `Qt::WA_QuitOnClose`, so that the window does not keep the application running after the other windows have closed;
- it sets `Qt::WindowDoesNotAcceptFocus`, so that an `activateWindow()` on it takes no focus from the main window and its keyboard shortcuts never fire;
- it fixes the size at the requested logical size, each side limited to the range from 200 to 2000 pixels, because a size change would build a new sender; and
- it moves the window to the top-left corner of the primary screen before it shows it, so that the popups that it opens are placed on a screen.

It refuses a null widget and a widget that has a parent, and returns false for them; calling it again on the same widget shows it again.

```cpp
QWidget hidden;
QComboBox choice(&hidden);
choice.addItems({"first", "second", "third"});
choice.setGeometry(10, 10, 340, 40);  // a target that is large enough for a tap in AR
qqs::show_offscreen(&hidden, 360, 240);  // shown to Qt, never to the desktop
if (!stream(*manager, "hidden", hidden, -0.7f)) { return 1; }
```

A popup is a window of its own that does not inherit those attributes, so `show_offscreen` also installs a guard that sets `Qt::WA_DontShowOnScreen` on every combo box list, menu and completer list that an off-screen panel opens, and on a tooltip window that has the panel as its parent, before Qt can map it. A tooltip of Qt on Windows has the desktop as its parent, so the guard does not cover it. The list stays visible to Qt, so the capture paints it into the picture of the panel where Qt placed it and AR input reaches it as before; it never appears on the desktop. Dialogs are the exception: they stay ordinary windows and become AR panels of their own. The popup guard acts on popups of off-screen panels only; the popups of a visible window stay desktop windows.

Tap the combo box of the second panel in the Visualizer and choose an item. The list opens in the picture of the panel and not on the desktop. The manager prints one line when the first popup opens over a panel and one when the last one closes. The first line has this form at its start, followed by the Qt class, the size and the position of the popup in the panel:

```text
popup open over this panel:
```

For a panel that is kept off the desktop, the first line has this form at its end:

```text
; kept off the desktop, drawn only into the AR panel
```

The second line is `popup closed`.

```powershell
Select-String -Path (Join-Path $env:TEMP 'qar_qt_panel_example.log') -Pattern 'popup' | ForEach-Object { $_.Line }
```

To let a user choose the size of such a panel, pass the text that they typed to `parse_offscreen_size`. It is a pure function: it takes a text of the form `WIDTHxHEIGHT` and the defaults, which are 480 by 720 pixels unless you give others, and returns a size that is always usable together with a warning that you should print when the text was not a size or had to be clamped; an unset or empty text gives the default without a warning.

## Closing a panel

A panel is closed through `close_panel(key, reason)`, the one door that every close initiated on the Qt side goes through. In the mode `destroy`, which is the default of the library, it cancels the pointers that are pressed on the panel, closes the AR panel, destroys its video sender and drops the stream; adding the same key again later is an ordinary new add. The call is safe from inside the click handler of the panel itself: a close that is asked for while a pointer action is still being delivered into the widget runs when that action has returned. The manager prints one summary line for each close, with the key, the reason, what each step did and how many frames the panel served.

The adapter closes a panel in three steps, in this order: it cancels the input subscriptions, closes the QarOS panel unless the operator closed it in AR, and destroys the video sender. QuaternAR states that the current runtime survives the destruction of a stream while other streams run, which is why the default destroys the sender.

The example asks the manager to close the counter panel when the counter window is hidden; with its `park` mode the AR panel is hidden and kept instead of closed. A window that has been closed on the desktop is only hidden, and a hidden widget is not painted, so the panel would go on streaming a blank rectangle. The example does not quit when its last window closes, so that you can watch the panel close while the hidden panel goes on streaming.

```cpp
if (!window.isVisible() && manager->has_panel("counter") && !manager->is_tombstoned("counter")) {
  manager->close_panel("counter", "closed on the desktop");  // hides its AR panel
}
```

Close the counter window on the desktop while the example runs. The adapter prints one line for each step of the close, each with the time of day. The line that opens the close has this form:

```text
closing: id=%s stream=%s (operator already closed it in AR: %s)
```

The line of the second step, which closes the QarOS panel, has this form when the call succeeds:

```text
step 2/3: qar_gui_panels_close_panel ran, ok
```

The line of the third step has this form:

```text
step 3/3: destroying the video sender
```

The line that completes the close has this form at its start, and says whether the GUI panel was closed and whether the video sender was destroyed:

```text
retired: id=%s stream=%s took=%lldms -- GUI panel %s,
```

The line of the manager starts with `closed`, names the panel and the number of frames that it served, and ends with the teardown of the three steps and the remark that the stream entry is gone. When the time is up, the manager ends the session for the panels that are still open: in the mode `park` or `notice` it hides them and detaches their video senders, in the mode `destroy` it closes them, and it reports that with lines that start with `ended the session for` or `closed`.

```powershell
Select-String -Path (Join-Path $env:TEMP 'qar_qt_panel_example.log') -Pattern "'counter' (closing|step|retired)|closed 'counter'|removed panel stream" | ForEach-Object { $_.Line }
```

If you close a panel in the Visualizer instead, the library stops capturing it and leaves its video sender alone: the sender is destroyed when the Qt window closes or when the session ends, and `qar_gui_panels_close_panel` is not called a second time for that panel.

The environment variable `QAR_PANEL_CLOSE_MODE` chooses what a close does. The value is read as one of three words, `destroy`, `notice` or `park`, in any case and without regard to the spaces around it; any other value gives the default and one warning that names the value. With `notice` the video sender is not destroyed: the stream goes on and the AR panel shows a notice that the window has closed, until you close the AR panel or the session ends. The mode is meant for a runtime on which destroying a sender while other streams run is not safe. With `park` nothing is closed while the session runs: the AR panel is hidden and kept, with its video sender, until the session ends, and a window that is shown under the same key shows it again; that mode is meant for a runtime on which even closing a QarOS panel is not safe. Run the example again with the variable set, and close the counter window.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QAR_QT_ROOT\plugins\platforms"
$env:QAR_PANEL_CLOSE_MODE = 'notice'
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --seconds 90 2>&1 | Select-String -Pattern "'counter'|stays up" | ForEach-Object { $_.Line }
```

The line that the manager prints in this mode says that the stream stays up and shows the panel-closed notice, and that the video sender is deliberately not destroyed.

## Integrating into your Qt application

The example is a short program, and the library asks the same of your application. Check these points in order.

- **Create the application object first and keep the manager on the GUI thread.** Every method of `QtPanelStreamManager` has to be called on the Qt GUI thread, which is also the only thread that may paint a widget or receive input for it; the injector refuses a delivery from any other thread.
- **Make the session owner first and poll it on every tick.** The owner joins the Hub on a thread of its own, so constructing it and calling `connect()` do not wait for QAROS. `add_panel` itself makes no call into QarOS, and the video sender and the panel are made and destroyed by tasks on the thread that owns the session, so no QarOS call that can wait for the Hub runs on the GUI thread.
- **Show a widget before you stream it.** `QWidget::render()` paints only the children that are visible, so a widget that was never shown gives its background and nothing else; the diagnostics of the stream report it as not visible.
- **Tick from a timer, and never pump AR input inside the handler of that timer.** A delivered click can open a modal dialog, which spins a nested event loop inside `QDialog::exec()`; Qt does not re-enter a timer whose handler is still on the stack, so a tick that pumped input would stop for as long as the dialog is open. Set `pump_input_in_tick` to false and call `pump_input()` from a posted event, as the example does.
- **Give every panel its own key.** `add_panel` refuses a key that is in use.
- **Close a panel when its window goes away, and keep the widget alive until then.** The manager holds a borrowed pointer to each widget. Call `close_panel` before you delete a widget; it is safe from the click handler of the panel itself.
- **Dialogs and popups need no code.** The lists of combo boxes and menus are painted into the picture of their panel. A dialog that a streamed panel opens becomes a panel of its own, placed beside its owner, and it closes with the dialog; while an application-modal dialog is open, the pointers that land on its owner panel reach nothing, as Qt blocks the window behind a modal dialog.
- **Include the library headers before the Qt headers.** `panel_pointer_state.hpp` declares a method named `emit`, which the `emit` macro of Qt would break; compile with `QT_NO_KEYWORDS` to lift the rule.
- **Shut down in order.** Destroy the manager first, while the widgets exist: it hides every panel that is still open and detaches its video sender. The factories and the session owner follow, because the session owns the senders.

The includes, the link line and the shutdown of the example are regions of its source.

```cpp
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
```

```cmake
target_link_libraries(qar_qt_panel_example PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_ogre_adapter::session
  qar_ogre_adapter::panels
  Qt5::Widgets
)
```

```cpp
manager.reset();  // hides the panels that are still open and lets go of their video senders;
                  // the factories and the session owner follow when main ends, in the reverse
                  // order of their declarations
```

A consumer of the installed package links the same targets; [API](API.md#linking-the-package) shows the `find_package` command.

## Next steps

- [Architecture](ARCHITECTURE.md) explains how the capture, the input path, popups, dialogs and closing are built, and which thread does what.
- [API](API.md) documents the public interface of the library.
- [Configuration](CONFIGURATION.md) lists the options and the environment variables.
- [Troubleshooting](TROUBLESHOOTING.md#panel-problems) explains what to do when a panel stays blank or does not react to AR input.
