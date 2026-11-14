# Verification

## Status

This section gives the result of each level. The results of Level 0 and Level 1 are those of this release, and each comes from a recorded run of the commands that the level names. The result of Level 2 comes from live sessions and live checks, and its line says which build each ran.

- Level 0, automated checks: passed. Every test of the repository that ran passed and none failed; a test that the run reports as skipped is not a failure ([Run all tests](#run-all-tests) shows the totals). The layering guard and its self-test reported OK ([The layering guard](#the-layering-guard)), and the consumer program of the install smoke test built and ran ([Install and the install smoke test](#install-and-the-install-smoke-test)).
- Level 1, headless checks: the connection check of the Qt example starts the QAROS Hub, so it is not run for this document ([A QarOS session without a window](#a-qaros-session-without-a-window)).
- Level 2, desktop checks: the maintainers ran the Qt example in a session with the desktop Visualizer of the installed QAROS 0.9.0-dev.3008+65dc9aeb, with Qt 5.15 and with Qt 6.8, and judged the checks as passed (taps, the list of a combo box, a resize, Ctrl+C, and a second start that joined without a second Allow); no session with a headset, no end of the program with a busy worker and no run with Qt 6.5 or Qt 6.10 has been made ([What the live sessions showed](#what-the-live-sessions-showed)).
- Level 3, device checks: not run for this release; [Device checks (Level 3)](#device-checks-level-3) says why.

## Verification levels

Every check of this document belongs to one of four levels. The level says what a check needs and who can run it, and it is the level of the documented commands that the check uses.

- **Level 0, automated checks.** They need neither the QarOS SDK nor a window and run in a plain PowerShell 7 window: configure, build, run the tests, install, the layering guard and the install smoke test. A script or a person runs them with the commands of this document, and every command has a recorded output.
- **Level 1, headless checks.** They run the real programs of this stack without a window, without a Visualizer, without inviting a peer and without a click. Most of them need the QarOS SDK: they open a real QarOS session and end again, which can leave QarOS background processes running.
A script or a person runs them.
- **Level 2, desktop checks.** They run against a real QarOS Visualizer on the desktop of the machine. A person starts the programs, points and clicks in the Visualizer and judges what it shows, and the logs of the run give the numbers. Nothing in this stack clicks, moves a window or presses a key.
- **Level 3, device checks.** They run against a real device, such as a HoloLens 2. Where no device is available, the command is run in its dry-run form and the outcome on a device is described from the code, with the log lines quoted from it.

A check of a higher level assumes that the lower levels passed. All checks run on Windows 11 on x64. A check that needs the QarOS SDK uses the folder that `QAR_SDK_ROOT` names, as [The QarOS SDK](INSTALL.md#the-qaros-sdk) describes, and a check that leaves QarOS background processes running does not stop them: if a later check cannot create a QarOS session, look for a program that holds UDP port 7446, as [Run-time problems](TROUBLESHOOTING.md#run-time-problems) explains.

## Automated checks (Level 0)

The automated checks need neither the QarOS SDK nor a window. Run them in PowerShell 7 from the clone, once the installation has reached [Build and install qar_qt_streaming](INSTALL.md#build-and-install-qar_qt_streaming). The commands are those of that section, of [Testing](TESTING.md) and of [Install smoke test](INSTALL.md#install-smoke-test), in the order of a full check. Level 0 passes when every command ends without an error and the test run reports no failed test. The Qt tests run under the offscreen platform plugin of Qt, which they copy next to the executable, so no window opens.

Go to the folder of the clone first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

### Configure and build

Configure with the preset of the repository, then build. The build compiles the library, the Qt example, the documentation snippets and the test executable into the build folder of the repository.

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"
...
-- Qt 5.15.2: C:/qar/deps/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5
-- QarOS SDK headers: QarOS repository C:/qar/src/qar_qt_streaming/external/qaros
...
-- Configuring done (12.5s)
-- Generating done (0.3s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
  qar_qt_streaming_seam.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\seam\RelWithDebInfo\qar_qt_streaming_seam.lib
...
  qar_qt_streaming_qaros.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\qaros\RelWithDebInfo\qar_qt_streaming_qaros.lib
...
  qar_qt_streaming_seam_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\seam\test\RelWithDebInfo\qar_qt_streaming_seam_tests.exe
  qar_qt_streaming_qaros_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\qaros\test\RelWithDebInfo\qar_qt_streaming_qaros_tests.exe
...
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
```

### Run all tests

The test run is the core of Level 0. It runs every test of the repository through the test preset. A pass is a run in which every test passes: the last lines of the output give the number of tests that ran and say that none failed. A test that is reported as skipped does not count as a failure.

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
        Start   1: check_layering_qar_qt_streaming
  1/832 Test   #1: check_layering_qar_qt_streaming ................................................................   Passed    0.36 sec
        Start   2: check_layering_examples
  2/832 Test   #2: check_layering_examples ........................................................................   Passed    0.08 sec
        Start   3: check_layering_selftest
  3/832 Test   #3: check_layering_selftest ........................................................................   Passed    0.14 sec
        Start   4: CommonName.SanitizeKeepsLowerCaseLettersDigitsAndDashesAndMapsTheRestToDash
...
100% tests passed, 0 tests failed out of 832

Total Test time (real) =  72.63 sec

The following tests did not run:
	626 - OffscreenPopupGuard.TheWindows11StyleComboListReachesThePanelImage (Skipped)
	627 - OffscreenPopupGuard.TheWindows11StyleListOfAVisiblePanelReachesThePanelImage (Skipped)
```

[Testing](TESTING.md#what-the-automated-tests-do-not-cover) lists what these tests leave out: every call into QarOS and the live desktop. Of the checks of the next levels, none was run for this document: `--connect-only` of Level 1 starts the QAROS Hub. The calls that create and drive panels were exercised against the real runtime only in the live checks on the desktop Visualizer.

### The layering guard

The guard checks the include rules of the library and of the example, and its self-test checks the guard itself. It prints one line for each module. [The layering guard](TESTING.md#the-layering-guard) explains the rules.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --all
```
```text
check_layering examples: OK (3 files)
check_layering qar_qt_streaming: OK (103 files)
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --self-test
```
```text
check_layering self-test: OK
```

### Install and the install smoke test

Install the library and the CMake package into the prefix, then build and run a small consumer program against the installed package. A pass is a consumer that prints the version of the package and the target that it found.

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_qt_streaming_seam.lib
-- Installing: C:\qar\install/lib/qar_qt_streaming.lib
-- Installing: C:\qar\install/lib/qar_qt_streaming_qaros.lib
-- Installing: C:\qar\install/include
-- Installing: C:\qar\install/include/qar_qt_streaming
-- Installing: C:\qar\install/include/qar_qt_streaming/seam
...
-- Installing: C:\qar\install/share/qar_qt_streaming/sdk/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfig.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfigVersion.cmake
```

```powershell
cmake -S docs\snippets\consumer -B build\consumer -G "Visual Studio 16 2019" -A x64 "-DCMAKE_PREFIX_PATH=$env:QAR_PREFIX;$env:QAR_QT_ROOT"
```
```text
-- Selecting Windows SDK version 10.0.22621.0 to target Windows 10.0.26200.
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- Found Threads: TRUE
-- Configuring done (3.9s)
-- Generating done (0.0s)
```

```powershell
cmake --build build\consumer --config RelWithDebInfo
```
```text
  Building Custom Rule C:/qar/src/qar_qt_streaming/docs/snippets/consumer/CMakeLists.txt
```

```powershell
.\build\consumer\RelWithDebInfo\qar_qt_streaming_consumer.exe
```
```text
qar_qt_streaming 0.3.0
targets: qar_qt_streaming::qar_qt_streaming
arm's-length preset: 0.50 mm per logical pixel, render scale 2.0
a 420 x 200 widget: buffer 420 x 200 px, panel 0.504 x 0.309 m
close mode of "notice": notice
```

## Headless checks (Level 1)

The headless check runs the Qt example without a window and without a Visualizer. It needs the installed QAROS, and it joins the Hub of the PC.

### A QarOS session without a window

With `--connect-only` the example creates no application object and no window. It joins the QAROS Hub, prints a description of the session and the id of the session, and ends again. A pass is an exit code of 0 and a line with the id of the session. A join that fails makes the example print the reason and exit with 1. The command starts the Hub when it is not running and the Hub asks its operator to allow the program, so a person runs it and it is not run for this document. [Check the connection to QarOS without a window](GETTING_STARTED.md#check-the-connection-to-qaros-without-a-window) describes the output.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --connect-only
```

## Desktop Visualizer checks (Level 2)

The Qt example runs against a real Visualizer: it joins the Hub, streams a counter window and a hidden off-screen widget into AR, and stops by itself after the time that `--seconds` gives. You start the example, you look at the Visualizer and you tap in it; nothing in this repository clicks or moves a window. The commands are those of [Getting started](GETTING_STARTED.md), and each check below names its section.

- **Stream one widget into AR.** [Stream one widget into AR](GETTING_STARTED.md#stream-one-widget-into-ar) starts the example. A pass is the Visualizer that you opened, with a receiver added, showing two panels, the counter window with its button and the label, and a second panel with a combo box. The backend reports a line for each panel when the QarOS panel exists, and the picture of the counter panel is readable.
- **AR input on the panel.** [Use AR input on the panel](GETTING_STARTED.md#use-ar-input-on-the-panel): tap the button of the counter panel in the Visualizer a few times. A pass is a label that counts the taps and one line of the example for each tap, and a hover that highlights the button.
- **A popup of the off-screen panel.** [The hidden off-screen panel idiom](GETTING_STARTED.md#the-hidden-off-screen-panel-idiom): tap the combo box of the second panel and choose an item. A pass is a list that opens inside the AR panel and never on the desktop, with a line when the popup opens and a line when it closes.
- **Closing a panel.** [Closing a panel](GETTING_STARTED.md#closing-a-panel): close the counter window on the desktop while the example runs. With the example's default mode `park`, a pass is an AR panel that is hidden when the window closes, and one line of the manager that says the panel was parked. In the close mode `destroy` the AR panel closes instead, with a line of the backend for each step of the close and the line that completes it; in the close mode `notice` the stream stays up and the AR panel shows a notice. The same section runs the example again with each of those two modes.

Dragging a slider or a scrollbar in AR and the use of two hands are covered by unit tests, because the counter window has no such control. Drags were exercised live on the RViz dock panels that `qar_rviz_plugin` streams through this library (expanding a tree item, dragging a slider, scrolling); a live check of a drag in the Qt example needs a widget with a scrollbar and is not part of this document.

### What the live sessions showed

The maintainers ran the Qt example in a session with the desktop Visualizer of the installed QAROS 0.9.0-dev.3008+65dc9aeb, once with Qt 5.15 and once with Qt 6.8. The example joined the Hub of the PC as a local app, the maintainers added a receiver in the Visualizer, and the example streamed its counter window and its hidden panel to it. They tapped the button and the list of the combo box, resized the window, ended the example with Ctrl+C, and started it again, which joined without a second Allow. The maintainers judged those checks as passed; the judgement is theirs, and this document gives no measurement of it.

Not run: a session with a headset, the end of the program with a worker of the backend that stays busy, and the example with Qt 6.5 and Qt 6.10. The drag of a slider and the use of two hands are covered by unit tests, the tap count and the long press by neither a unit test nor a live session, and no live session of the Qt example has exercised any of the four; drags were exercised live on RViz dock panels through `qar_rviz_plugin`, and two hands, the tap count and the long press in no live session.

## Device checks (Level 3)

Not run for this release: no device was available. The authors of qar_qt_streaming state that an earlier build of this stack worked on a HoloLens 2 with a QAROS runtime earlier than the verified version; that is their statement, and no record of that run exists.

None of the checks of this repository needs a device. The example can ask the Hub to start the app of a device that a connection string names, with its option `--invite`; that path was not run against a device.

## Feature coverage

The table lists what this repository provides, the automated suites that exercise it, and the levels whose checks in this document cover it. A suite is the name of a group of tests, the part before the first dot of a test name, and [List and select tests](TESTING.md#list-and-select-tests) shows how to list every test. The table gives names and no counts; the test counts are in the output of [Run all tests](#run-all-tests). Level 0 is the suites of the middle column, and a feature without a suite has only the checks of the higher levels. Level 2 is the desktop check that the maintainers ran ([What the live sessions showed](#what-the-live-sessions-showed)); an entry for Level 2 names a check of this document that they ran. The check of Level 1 opens a session and creates no panel, so no entry names it.

| Feature | Automated suites (Level 0) | Covered by the checks of levels |
|---|---|---|
| Qt widgets streamed as AR GUI panels: geometry, capture and the stream of a panel | `PanelGeometry`, `WidgetCapture`, `PanelStream`, `PanelManager`, `HashPixels`, `Readability` | 0; [2](#desktop-visualizer-checks-level-2) |
| Hit test and the conversion of a pointer to widget coordinates | `PanelHitTest`, `PanelRay`, `PanelUvToWidgetPx`, `PanelPicker`, `PanelHitToWidgetPoint`, `PanelUvToRoomPoint` | 0; [2](#desktop-visualizer-checks-level-2) |
| AR pointer input on real widgets: pointer states and injection | `PointerState`, `PointerActionKind`, `InputInjector`, `PanelStreamInput` | 0; [2](#desktop-visualizer-checks-level-2) |
| Popups inside the AR panel | `PopupOwnership`, `PopupCapture`, `OffscreenPopupGuard` | 0; [2](#desktop-visualizer-checks-level-2) |
| A dialog as a panel of its own | `DialogOwnership`, `DialogModality`, `DialogCapture`, `DialogAttribution`, `DialogInput`, `DialogPanels`, `NestedEventLoop` | 0 |
| Closing a panel with its window: the modes destroy and notice | `PanelClose`, `PanelCloseModeParse`, `PanelCloseNoticeMode`, `PanelLifetime`, `PanelTombstoneSizing`, `PanelCloseReport` | 0; [2](#desktop-visualizer-checks-level-2) |
| Widgets at a fractional device pixel ratio of 1.25 and 1.5 | `FractionalDpr` | 0 |
| Off-screen panels | `OffscreenSizeParse`, `OffscreenPanel`, `OffscreenStream` | 0; [2](#desktop-visualizer-checks-level-2) |
| Video panels for raw and compressed images | `RawImageDecode`, `CompressedImageDecode`, `AspectFitRect`, `LatestVideoFrame`, `VideoPanelWidget` | 0 |
| Live video inside a region of a panel, in place of an embedded native window | `RegionSubstitution`, `PanelRegionSourceManager` | 0 |
| Pacing of the capture for smooth video | `CaptureGatePure`, `TickIntervalEstimatorTest`, `FrameArrivalCapture`, `SubmitGapWindow` | 0 |
| Timing files and the frame trace | `FrameTrace`, `StreamTiming` | 0 |
| The values and interfaces of a panel, the common names, the guard of the panel state and the mapper of the gestures of QarOS | `Values`, `CommonName`, `PanelAttach`, `PanelStateDecision`, `PanelStatePoll`, `PanelGestureMapper`, `PointerGestureMapping`, `TimingLog` | 0 |
| The backend over the QarOS C++ SDK: create chain, frames, placement, close order, shutdown | `BackendTest`, `SessionExecutor`, `Conversions` | 0; [2](#desktop-visualizer-checks-level-2) |
| The layering of the library | `check_layering.py` | 0 |


## Known issues

No known issue is specific to this repository.

## Reporting a problem

When a check fails, report it with the facts that let someone repeat it. [What to send us](TROUBLESHOOTING.md#what-to-send-us) lists what every report needs: the facts of your machine, the newest QarOS logs and the steps that lead to the problem. A failed check of this document adds the following.

- **The check.** Its heading in this document, and the command exactly as you ran it, with its complete output.
- **The text of the console** in which the check ran, when a program printed to it.
- **The QarOS logs** that were written during the run, newest first, and every crash report that appeared: the files of the QarOS log folder whose names end in crash.txt, with their dumps, and the report that Windows shows for a program that stops.
- **Pictures.** A screenshot of the Visualizer, and of the window of your program where there is one, at the moment of the failure, when the failure is visible.
- **What you did,** step by step, and what you expected to see.
