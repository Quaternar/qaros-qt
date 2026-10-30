# Troubleshooting

This page lists the problems that you may meet with this library, by what you see. The first part is the index, the next four parts cover the build, the run time, the logs and what to send us, and the last part, [Panel problems](#panel-problems), covers what is specific to a streamed widget. Each problem has the same three parts: the symptom, the cause and the fix.

## Symptom index

| Symptom | Section |
|---|---|
| The configure step stops because a folder is not set, or a package is not found | [The configure step cannot find Qt or qar_ogre_adapter](#the-configure-step-cannot-find-qt-or-qar_ogre_adapter) |
| The compiler stops in `panel_pointer_state.hpp` at a method named `emit` | [The compiler stops at a method named emit](#the-compiler-stops-at-a-method-named-emit) |
| A path is too long, a script is refused, a command fails in Windows PowerShell, or a download fails | [Installation and build problems](#installation-and-build-problems) |
| A Qt program cannot find its platform plugin | [A Qt program cannot find its platform plugin](#a-qt-program-cannot-find-its-platform-plugin) |
| Creating a QarOS session fails, or the QarOS library crashes while it starts | [Run-time problems](#run-time-problems) |
| The stream stutters when its window is covered, or runs on the wrong graphics adapter | [Run-time problems](#run-time-problems) |
| A panel is blank, or shows only its background | [A panel is blank or shows only its background](#a-panel-is-blank-or-shows-only-its-background) |
| A panel is white where an embedded native window should be | [A panel is white where a native window should be](#a-panel-is-white-where-a-native-window-should-be) |
| The AR view stops while a dialog is open | [The AR view stops while a dialog is open](#the-ar-view-stops-while-a-dialog-is-open) |
| The panel is built again and again | [The panel keeps rebuilding](#the-panel-keeps-rebuilding) |
| A tap lands beside the target | [Clicks land beside the target](#clicks-land-beside-the-target) |
| The panel streams and nothing reacts to AR input | [The panel streams but takes no AR input](#the-panel-streams-but-takes-no-ar-input) |
| A pointer does nothing, and the injector says why | [An AR action is dropped](#an-ar-action-is-dropped) |
| A list, a menu or a dialog is missing in AR, appears twice, or is cut off | [A list, a menu or a dialog is missing in AR, or cut off](#a-list-a-menu-or-a-dialog-is-missing-in-ar-or-cut-off) |
| A scroll track shows at the right edge of the AR panel, or bands are left above and below the picture | [A scroll track shows in the AR panel](#a-scroll-track-shows-in-the-ar-panel) |
| A panel stays in AR after its window closed | [A panel stays in AR after its window closed](#a-panel-stays-in-ar-after-its-window-closed) |
| The log says that QarOS has a panel that is not being served | [The log says that a panel is not being served](#the-log-says-that-a-panel-is-not-being-served) |
| The text is blurry, or the panel is too small or too large | [The text is blurry, or the panel is too small or too large](#the-text-is-blurry-or-the-panel-is-too-small-or-too-large) |
| The runtime keeps asking for a stream that was closed | [The runtime keeps asking for a stream that was closed](#the-runtime-keeps-asking-for-a-stream-that-was-closed) |
| Where do the lines of the library go, and what do they mean | [Reading the logs](#reading-the-logs) |

## Installation and build problems

### A deep folder breaks the configure step

**Symptom.** Configuring fails while it downloads or unpacks GoogleTest, with an error about a file name or path that is too long (the Windows limit is 260 characters). **Cause.** The repository was cloned into a deeply nested folder, so the paths below its build folder exceed the limit. **Fix.** Use the short layout of this documentation, `C:\qar\src\<repository>`. Enabling long paths in Windows also removes the limit, but it is a machine-wide policy that this stack neither needs nor changes.

### PowerShell refuses to run a script

**Symptom.** PowerShell reports that running scripts is disabled, or asks whether you want to run a script that came from the internet. **Cause.** The execution policy of your machine blocks unsigned scripts, or the files carry the mark that Windows puts on downloads. **Fix.** Start the script through `pwsh` with its `-ExecutionPolicy` option set to `Bypass`, or remove the mark with `Unblock-File`; [Toolchain](INSTALL.md#toolchain) explains both.

### A command of this documentation fails in Windows PowerShell

**Symptom.** A command fails with a syntax error, or with an unknown command or parameter, in a window titled Windows PowerShell. **Cause.** Every command of this documentation was run in PowerShell 7 (`pwsh`); Windows PowerShell 5.1 is not what it was written and recorded for. **Fix.** Start `pwsh` and run the command there. The helper file `scripts\qar_locations.ps1` is the one script that states that it works in both editions.

### A download fails with a certificate error

**Symptom.** `pip`, `aqt`, `git` or `winget` stops with a certificate verification error, or CMake cannot download GoogleTest. **Cause.** Your network inspects TLS traffic and re-signs it with a company certificate that these tools do not trust by themselves. On the reference machine behind such a network, the downloads of `scripts\fetch_dependencies.ps1` worked, because they use the Windows certificate store, while the tools below needed the following settings.

- `pip`: point it at a bundle that contains your company root certificate with its `--cert` option.
- `aqt` and other Python programs: set the environment variables `SSL_CERT_FILE` and `REQUESTS_CA_BUNDLE` to the same bundle.
- `git`: tell Git to use the Windows certificate store by setting its option http.sslBackend to schannel. This changes your global Git configuration; undo it by unsetting the option again.
- `winget`: install with `--source winget`, as the commands of [Toolchain](INSTALL.md#toolchain) do. The default search also queries the Microsoft Store source, which can fail with a certificate error on such networks.
- CMake: download the GoogleTest archive in another way and use the offline option of the next entry.

The command below writes the root certificates of the Windows machine store into a bundle in PEM format and prints how many certificates it holds.

```powershell
$pem = foreach ($cert in Get-ChildItem -Path Cert:\LocalMachine\Root) {
    "-----BEGIN CERTIFICATE-----`n" + [Convert]::ToBase64String($cert.RawData, 'InsertLineBreaks') + "`n-----END CERTIFICATE-----"
}
$bundle = Join-Path $env:QAR_ROOT 'ca-bundle.pem'
Set-Content -Path $bundle -Value $pem -Encoding ascii
(Select-String -Path $bundle -Pattern 'BEGIN CERTIFICATE' | Measure-Object).Count
```
```text
76
```

### Configuring stops because GoogleTest is not available

**Symptom.** `cmake --preset vs2019-x64` ends with a fatal error that says GoogleTest 1.14.0 is not available and names network access to GitHub. **Cause.** Every repository downloads GoogleTest 1.14.0 as an archive at configure time and checks its SHA256, `8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7`; without access to `github.com` the download fails. **Fix.** Download `https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz` on a machine that has access, check the SHA256, unpack it, and configure with `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST` set to the unpacked folder `googletest-1.14.0`.

### The scripts cannot find Visual Studio 2019

**Symptom.** A script stops because `vswhere` finds no Visual Studio 2019 with the C++ x64 build tools. **Cause.** Only a newer Visual Studio is installed, or the Build Tools were installed without the C++ workload and the MSVC component; the scripts accept only a 16.x installation. **Fix.** Install the components listed in [Toolchain](INSTALL.md#toolchain) and run its verify command; a newer Visual Studio next to it does no harm.


### The configure step cannot find Qt or qar_ogre_adapter

**Symptom.** Configuring stops with a message that Qt 5.15.2 is not set, that a folder does not exist, or that the folder has no `Qt5Config.cmake` in its subfolder lib/cmake/Qt5; or with a message that qar_ogre_adapter 0.1.0 was not found. **Cause.** The location of Qt comes from `-DQAR_QT_ROOT` or, when that is unset or empty, from the environment variable `QAR_QT_ROOT`, and the folder has to contain the file `Qt5Config.cmake` of the Qt 5.15.2 build for MSVC 2019. The package qar_ogre_adapter is searched on `QAR_PREFIX` and `CMAKE_PREFIX_PATH`, so it has to be installed there first.
**Fix.** Set `QAR_QT_ROOT` as [Installation](INSTALL.md#directory-layout-and-environment-variables) says, install qar_ogre_adapter with the commands of [Upstream repositories](INSTALL.md#upstream-repositories), or build against a checkout of it with `-DQAR_OGRE_ADAPTER_SOURCE_DIR=<folder>`.

### The compiler stops at a method named emit

**Symptom.** A source file that includes a header of Qt before a header of this library fails to compile in `panel_pointer_state.hpp`, at the declaration of a method named `emit`. **Cause.** That header declares a method with this name, which only parses while the `emit` macro of Qt does not exist yet, and every Qt header that is not Qt-free defines the macro.
**Fix.** Include the headers of this library first, or define `QT_NO_KEYWORDS` for the target, which removes the macro.

## Run-time problems

### A Qt program cannot find its platform plugin

**Symptom.** A Qt program ends at start with a message that no Qt platform plugin could be initialised, or it does not start at all. **Cause.** Qt looks for its plugins relative to the program, and the Qt folder of this layout is not next to the program.
**Fix.** Put `$env:QAR_QT_ROOT\bin` on `PATH`, set `QT_QPA_PLATFORM_PLUGIN_PATH` to `$env:QAR_QT_ROOT\plugins\platforms` and `QT_PLUGIN_PATH` to `$env:QAR_QT_ROOT\plugins`.

### Creating a QarOS session fails because UDP port 7446 is held

**Symptom.** Creating the session fails with a QarOS error that names neither the port nor the program that holds it.
**Cause.** With a QAROS runtime earlier than the verified version, QarOS used UDP port 7446 for its discovery (zenoh scouting) and needed it free; the installed QAROS does not document a port for it. ROS 2 participants on domain 0, the default domain, take UDP ports in steps of two upward from 7410, so after about nineteen participants, for example test publishers that were never stopped, one of them holds 7446. On the domain with the number N the steps start at 7410 + 250 x N, so only domain 0 reaches 7446. **Fix.** Find the holder and stop that program yourself. The command prints the processes that hold the port, or says that it is free.

```powershell
$holders = Get-NetUDPEndpoint -LocalPort 7446 -ErrorAction SilentlyContinue
if ($holders) {
    $holders | ForEach-Object { 'UDP 7446 is held by process {0} ({1})' -f $_.OwningProcess, (Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).ProcessName }
} else {
    'UDP 7446 is free'
}
```
```text
UDP 7446 is free
```

### The QarOS library crashes while it starts

**Symptom.** The process ends with an access violation inside `qar_library_init`, and the call stack is in a `MSVCP140.dll` that lies next to the program. **Cause.** QarOS needs a Microsoft C++ runtime of version 14.40 or newer, and the process was using an older copy that lies next to the host program. The authors of this stack observed this in qar_rviz_plugin, where the older copy is the one that the ROS 2 distribution ships beside `rviz2.exe`.
**What the loader of this stack does.** The adapter loads `qar-streaming-c.dll` by its full path, with the flags `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` and `LOAD_LIBRARY_SEARCH_SYSTEM32` that the loader of the vendored header sets. Windows then searches for the dependencies of the library in the folder of the library first and then in the system folder, and never in the folder of the host program, the current folder or `PATH`. The runtime files in the QAROS `bin` folder therefore come before an older copy in the folder of the host program, and a runtime file that is missing from that folder is not taken from the folder of the host program. This is the order of the search and no guarantee: Windows uses a module of the same name that the process already holds, whichever folder it came from, so a runtime that the host program has already loaded stays in use.
**Fix.** In qar_rviz_plugin the authors of this stack got past the crash by replacing the runtime files next to `rviz2.exe` with the same files of version 14.4x from the system folder. If the crash remains, report it together with the versions of the `MSVCP140.dll` files on your machine.

### The stream stutters when the source window is covered

**Symptom.** The AR picture is smooth while the source window is in front and stutters, with a steady frame rate that is a fraction of the target, when another window, such as a full-screen Visualizer, covers it. **Cause.** From Windows 11 on, a process whose windows are fully covered may no longer get the timer resolution it requested.
**Fix.** Keep the source window visible, for example side by side with the Visualizer.

### The picture is rendered on the wrong graphics adapter

**Symptom.** On a laptop with an integrated and a discrete GPU, the frame rate is low or each frame is copied between adapters. **Cause.** Windows ran the program on the integrated GPU, while QarOS encodes on the NVIDIA GPU.
**Fix.** Set the graphics preference of the program to high performance in Windows Settings, under System, Display, Graphics.

### Logs grow very large, or all streams stall every 15 seconds

**Symptom.** The QarOS log folder holds files of a gigabyte or more, or every stream stops for about a second and a half at a steady 15-second rhythm. **Cause.** The library log severity is trace, which writes hundreds of thousands of lines in a short run, and a real-time virus scanner or a nearly full system drive turns that into stalls.
**Fix.** Run with the severity warn or higher, and use trace only for a log that QuaternAR asks for; [Reading the logs](#reading-the-logs) explains how the severity is decided.
The example of this repository runs at warn. The default of `log_severity` in the configuration of the session factory is trace, so a host of your own sets it to warn or higher, as the example does.

## Reading the logs

The library writes no log of its own, to no file and to no console. The manager reports through the `PanelDiagnosticsSink` of its configuration: every line has a severity, `Info`, `Warning` or `Error`, a category, which is always `panel`, a text and the key of the panel, and the host decides where it goes. Without a sink the manager keeps its counters and says nothing. The example prints every line with the word example and a colon, the key in square brackets and the text, and the table below lists the texts of the lines. The adapter of qar_ogre_adapter prints lines of its own to the standard error stream, each with the prefix `[qar-panel-adapter]`; [Getting started](GETTING_STARTED.md#stream-one-widget-into-ar) shows the lines of the creation and [Getting started](GETTING_STARTED.md#closing-a-panel) those of a close.

| Text of the line | Severity | What it means |
|---|---|---|
| `added panel stream` | Info | `add_panel` registered the widget; the QarOS panel follows on the first tick that finds the widget laid out |
| the geometry of the panel, which starts with the key | Info | a sender was just built, at the creation of the panel or after a resize that was adopted; it gives the logical size, the pixel ratio, the buffer, `content_scale sent` and the size in metres, which ends with the video and the four insets and the fit margin of the frame that QarOS draws around it, and, for the panel of a dialog, how the width was fitted |
| `popup open over this panel` and `popup closed` | Info | the first popup of a panel opened, and the last one closed; the first line says whether the popup is kept off the desktop |
| `dialog opened from` the owner, `is streaming as a panel of its own` | Info | a dialog was promoted to a panel |
| `could not give the dialog a panel of its own` | Warning | the promotion failed; the reason follows |
| `QarOS has a panel from us it is not being served` | Warning | a panel holds a sender and has submitted no frame for `unserved_warn_seconds`; [below](#the-log-says-that-a-panel-is-not-being-served) |
| `closed`, the key and the stream | Info | a close in the mode `Destroy` finished; the line names the frames that the panel served and what each of the three steps did |
| `having served it NO frames` | Warning | a panel was closed or removed though QarOS was told that its stream exists and it never got a frame |
| `could not be closed` | Warning | `qar_gui_panels_close_panel` failed; the sender was destroyed and the entry dropped regardless, but QarOS may still show the panel |
| `was asked for from inside a pointer action` | Info | a close was queued because it came from the handler of the panel itself |
| `the operator closed this panel in AR` | Info | capture stopped; the sender goes with the close of the window or the end of the session |
| `removed panel stream` | Info or Warning | the session ended, or a stream that never created a sender was erased; it names the frames that were served |
| `re-used the retired panel slot` | Info | `Notice` mode: a retired key was added again and its sender was reused |
| `this session is holding` panels whose windows closed | Warning | the cap of the modes `Notice` and `Park` is reached, and no further panel stream is created |
| `parked`, the key and the reason | Info | a close in the mode `Park` hid the panel in AR and kept it, with its sender, until the session ends |
| `revived parked` | Info | the key of a parked panel was added again; QarOS is asked to show the panel again, and no new QarOS panel is made |
| `"could not hide"` | Warning | QarOS refused the hide of a parked panel; the panel shows the notice instead, and nothing is closed |
| `could not show the revived panel` | Warning | QarOS refused the show; the panel stays hidden |
| `changed size:` and the key | Info or Warning | in the mode `Park` a size change hid and kept the old QarOS panel (Info); or QarOS refused the hide, or the limit of 32 kept panels is reached (Warning) |
| `fitted to` and the key | Info | a dialog was fitted before its panel was made; the line gives the size, the number of rounds and a note |
| `"went away after"` | Info | a dialog was hidden or destroyed while it was fitted, and it got no panel |
| `scroll census` | Info | the scroll areas of the widget, measured at the first frame, 1.5 s later and when the host asks |
| `still shows a vertical scroll bar` | Warning | a scroll bar that cannot be used in AR is still there 1.5 s after the first frame |
| `frame luminance` | Info | the mean luminance of the first frame, and of the first one after a long streak of unserved ticks |
| `the panel is too narrow to show its own name` | Warning | the notice of `Notice` mode is cut off |
| a line that counts the dialogs that are owned by the panel | Info | `dismiss_owned_dialogs` closed them |
| a bare reason such as `widget has a zero logical dimension (not laid out yet?)` | Warning | a stream could not capture; a reason is reported once, when it changes |

QarOS writes its own logs, separate from the logs of this stack. When something fails between your program and QarOS, the cause is often only in the QarOS logs, so you need to know where they are and how loud they are.

### Where the QarOS library writes its logs

The library writes its log files to the folder `quaternar` inside your `TEMP` folder, `%TEMP%\quaternar`, unless the program that initialises the library names another folder; an empty folder name selects the default. Every QarOS process that the library starts writes its own files, and the file names carry the role of the process and a GUID, for example `mixer_<guid>_stdout.log`. QuaternAR asks for full trace-level library logs with every bug report.

### Which severity applies

The library reads its severity from the environment variable `QAR_LOG_SEVERITY`, through the environment table of the C runtime, and that variable wins over the severity field of the library initialisation, in both directions: the field cannot make a run quieter than the variable, and the variable cannot be overridden by the field. A change made with the Windows function that sets an environment variable is not seen by the library; the adapter of this stack therefore sets the variable with the C runtime function before it initialises the library, and also passes the severity in the initialisation structure. The library accepts the spellings trace, debug, info, warn, warning, error and fatal, in any letter case, and no others; this stack accepts the same spellings.

### How much a trace log writes

A run at trace severity writes hundreds of thousands of lines in a short time. Use trace only for the log that QuaternAR asks for, delete it afterwards, and run at warn or higher otherwise. [Installation and build problems](#installation-and-build-problems) and [Run-time problems](#run-time-problems) describe what a large log does to a machine.

### The log of the transport

QarOS uses zenoh as its transport. When a session cannot be created, the C API reports only a generic transport error; the actual reason, such as a port that cannot be bound, is in zenoh's own log, which stays silent unless it is switched on. The adapter of this stack switches it on when the environment variable `QAR_ZENOH_LOG` holds a tracing filter, such as debug, and only while the library severity is trace or debug; at a quieter severity the request is ignored and the log says so. Setting `RUST_LOG` alone does nothing in these programs.

## What to send us

When you report a problem to the maintainers of this repository, send the facts below. They let someone reproduce your setup without a long exchange. The first command writes the facts of your machine into one text file and prints it: the commit of this repository, the Windows and tool versions, the variables of the layout, and the SHA256 of the SDK header, which identifies the QAROS version. The commit is the abbreviated hash, with the suffix "-dirty" when you changed files, or the name of a tag when the commit has one. It prints only the variables of this stack, no others.

Git reads the commit from the clone, so go to its folder first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
$out = Join-Path $env:TEMP 'qar_support_info.txt'
$info = @(
    'repository : ' + (git describe --tags --always --dirty)
    'windows    : ' + (Get-CimInstance Win32_OperatingSystem).Version
    'powershell : ' + $PSVersionTable.PSVersion.ToString()
    'cmake      : ' + ((cmake --version) | Select-Object -First 1)
)
$info += Get-ChildItem Env:QAR_*, Env:ROS2_INSTALL_ROOT | ForEach-Object { '{0} = {1}' -f $_.Name, $_.Value }
$info += 'sdk header  : ' + (Get-FileHash -LiteralPath (Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h') -Algorithm SHA256).Hash
$info | Set-Content -Path $out
Get-Content -Path $out
```
```text
repository : a4480d7
windows    : 10.0.26200
powershell : 7.4.4
cmake      : cmake version 3.29.5-msvc4
QAR_DEPS_ROOT = C:\qar\deps
QAR_VENV = C:\qar\venv
QAR_SDK_ROOT = C:\QarOS\install
QAR_QT_ROOT = C:\qar\deps\Qt\5.15.2\msvc2019_64
QAR_PREFIX = C:\qar\install
QAR_ROOT = C:\qar
ROS2_INSTALL_ROOT = C:\qar\deps\ros2_humble\ros2-windows
sdk header  : 5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

The line `cmake` shows the CMake of your machine. The output above is the one of the recorded runs, which used the CMake that Visual Studio 2022 version 17.11 bundles; [CMake](INSTALL.md#cmake) says what that means for the CMake that you install.

Then send the logs of the failing run. The QarOS log folder can hold gigabytes, so send only the newest files, those whose time matches the failure. The command lists the five newest files of the folder with their sizes; it prints nothing when QarOS has not written there yet.

```powershell
Get-ChildItem -LiteralPath (Join-Path $env:TEMP 'quaternar') -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object { '{0}  {1:N0} bytes' -f $_.Name, $_.Length }
```
```text
qar_streaming.log  818 bytes
qar_launcher_stdout.log  204,402 bytes
mixer_51cca380-fcfb-4c84-907e-8b668a9c9ee2_stdout.log  10,026 bytes
streaming_viz_b829d79d-b8a8-478e-bf23-c0d28ca52ad0_stdout.log  866 bytes
mixer_51cca380-fcfb-4c84-907e-8b668a9c9ee2_stderr.log  0 bytes
```

Send, in addition:

1. The file `$env:TEMP\qar_support_info.txt` that the first command wrote.
2. The newest files of the QarOS log folder, and a log made at trace severity if the problem is between your program and QarOS; [Reading the logs](#reading-the-logs) explains how to switch it on.
3. The steps that lead to the problem, in the order you did them, and what you expected to see. A screenshot helps when the problem is visible.
4. Anything you changed from this documentation: other folders, other versions, a proxy.

## Panel problems

### A panel is blank or shows only its background

**Symptom.** The AR panel exists, and it shows the background of the widget and none of its controls. **Cause.** `QWidget::render()` paints only the children that are visible, and visibility is inherited, so a widget that was never shown, or that was hidden, gives its background and nothing else. A window that is closed on the desktop is only hidden, so its panel goes on streaming a blank rectangle. The stream says so: `widget_visible` of its diagnostics is false.
**Fix.** Show the widget before you add it, with `show()` or, for a widget that is never to reach the desktop, with `show_offscreen`. Close the panel when the window is closed: `close_panel(key, reason)`, as the example does when its window is no longer visible. A widget that has just been shown is not laid out until Qt has handled its events, and a stream that finds no size reports `widget has a zero logical dimension (not laid out yet?)` once and tries again on the next tick.

### A panel is white where a native window should be

**Symptom.** The panel of a window that contains a camera view, a 3D view or another embedded native window shows a solid white rectangle in its place. **Cause.** A native window that is embedded with `QWidget::createWindowContainer()` has no paint event that shows what it displays, so `QWidget::render()` cannot draw it.
**Fix.** Paint video over that region instead: give the panel a `LatestVideoFrame` for the region with `set_panel_region_source`, and publish frames into it from a worker thread; [Video in panels](ARCHITECTURE.md#video-in-panels) explains the mechanism and [API](API.md#video-frames) shows the calls.

### The AR view stops while a dialog is open

**Symptom.** A tap in AR opens a modal dialog, and from then on nothing is captured or submitted, in any panel, until the dialog is closed on the desktop. **Cause.** The tick runs from a `QTimer`, and the input was delivered from inside its handler. The dialog spins a nested event loop in `QDialog::exec()`, which does not return until the dialog is dismissed, and Qt does not deliver a timer again while its handler is still on the stack. **Fix.** Set `pump_input_in_tick` to false and call `pump_input()` from a posted event, a queued call, as [Popups and dialogs](ARCHITECTURE.md#popups-and-dialogs) and the example do. A nested loop that is entered from a posted event leaves the timer free, so the stream goes on, and the dialog gets its own panel.

### The panel keeps rebuilding

**Symptom.** The picture of a panel flickers or stops, `adapter_rebuilds` in its diagnostics keeps climbing, and the manager logs the geometry line of the panel again and again. **Cause.** Every adopted change of the widget size is a new sender, a new panel and a new input subscription. A splitter that is dragged passes through every size on the way, and a widget that resizes itself, such as a status label inside the streamed widget whose text grows, changes the buffer on every update. **Fix.** Keep the size of the streamed widget steady: do not let a label inside it change the size hint, or give it a fixed width. The library already ignores a change below `resize_threshold_px` and waits for a larger one to hold for `resize_settle_seconds` and `resize_settle_ticks`; raise them for a widget that changes slowly, and look at `resizes_ignored_small` and `resize_pending` to see what the hysteresis did. A widget that never changes size, such as a panel that was shown with `show_offscreen`, never rebuilds.

### Clicks land beside the target

**Symptom.** A tap or a hover lands on a control next to the one that you aimed at, or only on a part of the panel. **Cause.** A position that QarOS reports is normalised against the content size of the panel, so the content size must match the widget. The library sends 1.0 as the content scale, so a panel of this library does not zoom, and the geometry line of the log reports `content_scale sent` as 1.0; a zoom above 1.0 would crop the far edge and push every contact off its target. A position beyond the content, for example after the widget was resized under a live subscription, is clamped to the edge. **Fix.** Look at the geometry line of the panel and at the line that the adapter prints when the subscription for AR input was accepted, which names the content size: it has to be the size of the widget in pixels, with the render scale and the pixel ratio in it. If the panel was resized, wait until the hysteresis has adopted the new size.

### The panel streams but takes no AR input

**Symptom.** The panel is in AR and nothing reacts to a tap or a hover. **Cause.** One of four. No input source was set in `QtPanelStreamingConfig`, so no subscription exists. Or the subscription failed, and the adapter prints a line with `streams but takes no AR input`, followed by the reason; a failed subscription is not fatal, because a panel without input is better than no panel. Or `PanelInputOptions::enabled` is false. Or the host set `pump_input_in_tick` to false and never calls `pump_input()`. **Fix.** The counter `input_samples_received` of the panel tells where to look: zero means that nothing arrived from QarOS, which is a matter of the subscription or of the Visualizer, and a number that grows with nothing happening in the widget means that the samples arrive and are dropped, which `input_events_dropped` and `last_input_drop_reason` explain.

### An AR action is dropped

**Symptom.** A pointer does nothing, and `stats().last_drop_reason` of the injector, or `last_input_drop_reason` of the panel, holds a reason. **Cause and fix.** The reasons are listed in [Input injection](API.md#input-injection). The three that you meet most are these. "not on the Qt GUI thread that owns the panel widget": the call came from a worker thread; call the manager from the GUI thread. "a second pointer pressed while another one owns the synthesized mouse": a desktop has one mouse, so while one pointer presses a plain widget a second pointer can only use a widget that accepts touch events, and the standard controls do not. "no widget under the pointer accepts touch events (touch-only mode)": the delivery mode is `TouchOnly` and the widgets do not accept touch; use the default `TouchWithMouseFallback`.

### A list, a menu or a dialog is missing in AR, or cut off

**Symptom.** A combo box list, a menu or a tooltip opens on the desktop and not in the AR picture, a list is cut at the edge of the panel, or a dialog does not get a panel. **Cause.** A popup belongs to a panel by the parent walk, so a menu that was created without a parent, `new QMenu()` and not `new QMenu(this)`, belongs to nobody and is not drawn. A popup that hangs over the edge of its panel is clipped, never shifted, because shifting the drawing would put every item under a different item of the hit test. A dialog belongs to the panel whose tree is its parent, or else to the panel that took the latest activating AR action within 1500 ms; a dialog that you opened with the mouse has no owner and stays on the desktop. A dialog that a hidden panel opens appears on the desktop as well, because an off-screen panel keeps only popups off the desktop, and a tooltip of Qt on Windows is not covered either. **Fix.** Give the menu a parent. Give the panel enough room for its lists, and read `popup_clipped()` to see that a list was cut. Open the dialog from AR, or pass the panel as its parent.

### A panel stays in AR after its window closed

**Symptom.** A window was closed and its panel is still there, either blank or with a notice. **Cause.** The library does not watch windows: it closes a panel when the host asks, with `close_panel`, and a window that is closed on the desktop is only hidden. In the mode `notice`, which `QAR_PANEL_CLOSE_MODE=notice` chooses in the example, the panel stays by design and shows that the window has closed until the operator closes it in AR. In the mode `park`, which `QAR_PANEL_CLOSE_MODE=park` chooses, the panel is hidden and kept with its sender until the session ends. **Fix.** Call `close_panel(key, reason)` when the window closes; in the default mode it closes the AR panel and destroys the sender, and in the mode `park` it hides the panel and keeps it; at the end of the session the panels of the modes `park` and `notice` are hidden and detached, and the next start finds them again. If the log has a line with `could not be closed`, `qar_gui_panels_close_panel` failed, the sender and the entry are gone on this side, and QarOS may still show the panel until the session ends.

### The log says that a panel is not being served

**Symptom.** The manager logs a warning that starts with `QarOS has a panel from us it is not being served`, with the key, the stream id, the seconds and the number of consecutive not-ready ticks, and it repeats once for every run. **Cause.** A sender exists before anything in the headset has attached to its stream, and the runtime reports that as status 604, which the library treats as not ready and not as an error. The state is normal for a moment, which is well under a second, and a panel that stays in it for `unserved_warn_seconds`, 5 s by default, has no receiver: the Visualizer or the headset is not connected, or it has not asked for this stream. QarOS asks again for a stream that it was promised and never received, for as long as the session lives. **Fix.** Check that the Visualizer is running and joined to the session. If it is, search the log of the runtime for the stream id that the line names; every line that the runtime writes about the panel carries it. [Reading the logs](#reading-the-logs) says where that log is.

### A scroll track shows in the AR panel

**Symptom.** QarOS draws a scroll track along the right edge of a panel, and the picture is narrower than the panel, or the picture leaves a band above and below it. **Cause.** QarOS draws the video of a stream panel inside a frame of its own, and the library makes the panel for a frame of its own, `PanelFrame`: by default a top inset of 0.066 m and 0.003 m at the other three insets and in the fit margin, the values that were measured on the stream panels of the desktop Visualizer. A video that is taller than the area between the top and the bottom inset is scrolled behind a track that takes a strip off its width, and a video that is shorter leaves a band above and below it. A runtime whose title bar or border differs from the default leaves one of the two. The scroll bars of Qt inside the widget are another matter, and the lines `scroll census` and `still shows a vertical scroll bar` of the log name them.
**Fix.** Set `ar_frame` in the `QtPanelStreamingConfig` to the insets of your runtime. The geometry line of the log gives the video and the frame that the panel was made with, and a frame of all zeros makes the panel exactly as tall as its video. Keep the fit margin above the error of your measurement of the title bar, so that the video is always a hair shorter than the area.

### The text is blurry, or the panel is too small or too large

**Symptom.** The text in AR is unreadable, or the panel hangs at a size that is not what you intended. **Cause.** The physical size of a panel is the logical size of the widget times `meters_per_logical_px`, 1.2 mm by default, and the sharpness is the pixel ratio of the display times `render_scale`; a buffer is limited to 4096 pixels on a side, and a widget that needs more is streamed at a lower ratio. A denser display makes the picture sharper and not larger. **Fix.** Set `meters_per_logical_px` for the size, or derive it with `recommend_readability` and `apply_readability` from the viewing distance, and raise `render_scale` for sharpness; the geometry line of the log shows the effective ratio that the buffer ended up with. [DPI, buffers and AR readability](ARCHITECTURE.md#dpi-buffers-and-ar-readability) has the arithmetic.

### The runtime keeps asking for a stream that was closed

**Symptom.** After a panel was closed, the log of the QarOS runtime keeps naming the stream of that panel: a line says that the connection for the stream was lost and that the receiver enters its reconnecting state, and every ten seconds or so a retry fails with the status 507, until the producer peer goes away. **Cause.** The C API has no call that withdraws a logical video stream: `qar_video_sender_handle_destroy` returns nothing and `qar_gui_panels_close_panel` closes the panel and not the stream behind it, so a receiver that was attached is left reconnecting, and nothing on the producer side can stop it. **Fix.** None on this side. The close line of the manager carries the stream id and the number of frames that were served, so that you can find the retries with one search for the id; [Known SDK limitations](SDK_COMPATIBILITY.md#known-sdk-limitations) says what the authors of qar_qt_streaming observed.
