# Testing

## Running the tests

All tests of this repository are registered with CTest and run through the test preset `vs2019-x64`. The preset runs the configuration `RelWithDebInfo` and prints the output of every test that fails. Configure and build first, as described in [Build and install qar_qt_streaming](INSTALL.md#build-and-install-qar_qt_streaming); the tests are built with the rest.

The tests need no QarOS SDK and open no window, and you do not have to prepare the shell: a plain PowerShell 7 window is enough, because each test executable finds its libraries by itself.

- A Qt test executable gets the Qt DLLs copied next to it after it is built.
- The Qt tests run under the offscreen platform plugin of Qt, which they copy next to the executable as well, so no window is ever created.

### Run all tests

Every command of this document runs in the folder of the clone; go there first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
        Start   1: check_layering_qar_qt_streaming
  1/420 Test   #1: check_layering_qar_qt_streaming ...........................................................   Passed    0.17 sec
        Start   2: check_layering_examples
  2/420 Test   #2: check_layering_examples ...................................................................   Passed    0.04 sec
        Start   3: check_layering_selftest
  3/420 Test   #3: check_layering_selftest ...................................................................   Passed    0.05 sec
        Start   4: AlignUp.RoundsUpToTheNextMultiple
  4/420 Test   #4: AlignUp.RoundsUpToTheNextMultiple .........................................................   Passed    0.01 sec
...
100% tests passed, 0 tests failed out of 420

Total Test time (real) =  30.54 sec
```

The last lines are the summary: how many tests passed, and the names of those that failed. A test that is skipped is reported as skipped and does not count as a failure.

### List and select tests

Every GoogleTest test is registered as one CTest test named `Suite.Test`, and the guard tests of the repository have names that start with `check_layering`.
The first command lists the registered tests without running them; the second runs only the tests whose name matches a regular expression, here the self-test of the layering guard.

```powershell
ctest --preset vs2019-x64 -N
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
  Test   #1: check_layering_qar_qt_streaming
  Test   #2: check_layering_examples
  Test   #3: check_layering_selftest
  Test   #4: AlignUp.RoundsUpToTheNextMultiple
  Test   #5: AlignUp.AlignmentOfZeroOrOneIsIdentity
  Test   #6: PanelGeometry.AtRatioOneBufferMatchesLogicalSize
  Test   #7: PanelGeometry.HiDpiGrowsTheBufferButNotThePhysicalSize
...
  Test #418: StreamTiming.AnUnchangedFrameIsRecordedAsSuchAndAFramelessTraceLeavesBlanks
  Test #419: StreamTiming.APanelThatIsNotVideoDrivenWritesNoVideoFile
  Test #420: StreamTiming.WithTimingOffTheStreamBehavesAsBeforeAndWritesNothing

Total Tests: 420
```

```powershell
ctest --preset vs2019-x64 -R check_layering_selftest
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
    Start 3: check_layering_selftest
1/1 Test #3: check_layering_selftest ..........   Passed    0.05 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.07 sec
```

Use `-R` with a part of a suite name to run one suite, and add `-j` followed by a number to run tests in parallel. When a test fails, its output is printed by the preset; add the option `--rerun-failed` to the command to run only the tests that failed in the last run.

## What the tests cover

The unit tests are one GoogleTest executable, `qar_qt_streaming_tests`, and the layering guard adds its own tests. The list command above prints every registered test by name, with the number of tests at the end. Unlike the tests of the Ogre side, these tests drive the real Qt paint path, because the capture of a widget with `QWidget::render()` is what the library exists for.

| Suites | What the tests assert |
|---|---|
| `PanelGeometry`, `AlignUp`, `Readability` | the buffer follows the logical size times the pixel ratio and is aligned up to an even size; the physical size follows the logical pixels and never the buffer; an oversized widget lowers the ratio and not the panel size; the content scale that is sent is always 1.0; the readability presets reproduce the worked example, and the distance changes the physical size but not the render scale |
| `HashPixels`, `WidgetCapture` | a capture has the requested size; an untouched widget reports no change and an edit makes the next capture dirty; padding at the end of a row is not hashed; a hidden widget still captures but is flagged |
| `PopupOwnership`, `PopupCapture` | a popup of the panel is owned and the popup of another panel is not; an open popup lands where Qt placed it, honours the render scale, vanishes when it closes and is not sent again every tick; the list of a real combo box reaches the stream |
| `DialogOwnership`, `DialogModality`, `DialogCapture`, `DialogAttribution` | an owned dialog is never drawn into its owner; a dialog that is parented to the shared window is attributed to the panel that took input last; only the attributed panel is blocked by its modal dialog |
| `PanelStream` | the first tick creates the sender and submits; an unchanged widget is captured and not sent; the keepalive resends it; the frame-rate limit sheds ticks; a size change rebuilds the sender only when it is large and has settled, and a width that oscillates never does |
| `PanelManager`, `QtPanelStreamManager` | construction without a factory is refused; adding a panel calls QarOS not at all before the first tick; duplicate keys and bad arguments are refused; a room point is routed to the pixel under it; a panel that holds a sender and serves no frame is warned about once; a popup is logged once when it opens and once when it closes |
| `PanelStreamInput`, `NestedEventLoop`, `DialogPanels` | an instant click through the manager presses the real button and a pressed pointer stays with its panel; the queued source is drained on tick unless the host owns the pump; `pump_input` survives being entered again by a nested event loop; a timer whose handler enters a loop is not entered again, and one that enters it from a posted event keeps firing; a dialog becomes a second panel that is sized from the dialog and closes with it |
| `PointerState`, `PointerActionKind` | an instant click is enter, press, release and click; a drag off the panel stays grabbed and a release outside is no click; a cancel is never a click; two quick taps in place are a double click; pointers are independent |
| `InputInjector`, `DialogInput`, `DialogAttribution` | a click fires a push button, a check box toggles, a slider follows a press and a drag, a combo box opens its popup and takes a selection; touch-aware widgets get touch sequences; the delivery modes differ as documented; a delivery off the GUI thread is refused; a destroyed widget is survived; a modal dialog blocks the panel behind it |
| `PanelHitTest`, `PanelRay`, `PanelUvToWidgetPx`, `PanelPicker`, `PanelHitToWidgetPoint`, `PanelUvToRoomPoint` | the pose sits on the top edge of the panel, u grows to the right and v downwards, the front is at negative local z, and a hit maps back to the room; a ray parallel to the panel never lands |
| `PanelClose`, `PanelCloseReport`, `PanelCloseNoticeMode`, `PanelCloseModeParse`, `PanelCloseModeNames`, `PanelCloseModeConfig` | destroy is the default; a close closes the panel and destroys the sender once and in order; a close that fails still destroys the sender, drops the entry and warns; a close asked for from the click handler of the panel runs when the click has returned; the notice mode keeps the sender; an unknown close mode gives the default and one warning |
| `PanelLifetime`, `PanelTombstoneSizing`, `PanelTombstoneWidget` | in the notice mode a closed source keeps the stream and the sender; the notice takes no input; a close in AR stops the capture and does not destroy the sender; the session shutdown closes every panel exactly once; a retired slot is reused; the number of retired panels is capped |
| `OffscreenSizeParse`, `OffscreenPanel`, `OffscreenStream`, `OffscreenPopupGuard` | the parser of the size; the attributes, the flag, the fixed size and the position that `show_offscreen` leaves; a tap opens a list that reaches the stream and is not a desktop window; new rows never rebuild the sender; the guard is installed once; stopping the stream closes the invisible popups; the list of a visible panel stays a desktop window |
| `CaptureGatePure`, `TickIntervalEstimatorTest`, `FrameArrivalCapture`, `SubmitGapWindow`, `FrameTrace`, `StreamTiming` | a panel without video keeps the strict limiter; video keeps 30 frames a second on a coarse tick and the frame-rate limit stays an upper bound; a stalled source falls back to polling so that the keepalive still fires; the timing rows of a video panel |
| `RawImageDecode`, `CompressedImageDecode`, `AspectFitRect`, `LatestVideoFrame`, `VideoPanelWidget`, `RegionSubstitution`, `PanelRegionSourceManager` | exact pixels for each encoding, padding at the end of a row, normalised 16-bit and float samples, rejection of bad input, down-scaling that keeps the aspect ratio; a PNG round-trips exactly; the mailbox is safe under concurrent publishing; a video frame fills its region, and a destroyed region is ignored |

## Test doubles

The tests replace the QarOS side of the library, the clock and the receiver of resolved pointer events with small stand-ins that run in the same process. The stand-ins are in the header `mock_panel_sender.hpp`, which belongs to the interface library `qar_qt_streaming::test_support`; that library is only for the tests and is not installed. Two collaborators stay real: the Qt widgets, and the queue of pointer samples, `QueuedPanelInputSource` of `qar_ar_core`, into which the tests push samples the way the callback of QarOS does.

| Test double | What it stands in for |
|---|---|
| `MockQarPanelSenderAdapter` | the QarOS panel sender: it records every call to begin a frame, submit one, place the panel and close it, copies out the pixels of each submitted frame, records the steps of a close in order, and fails a begin, a submit or the `close_panel` step when the test asks it to |
| `MockQarPanelSenderFactory` | the factory of those senders: it counts the creations, can refuse to create one, can hand out senders that never report ready, and keeps the order of every create and close across all the senders that it made |
| `RecordingPanelInputSink` | the receiver of resolved pointer events: it records which panel, which pixel, and whether the event was a hover or a click |
| `ManualClock` | the clock of the manager: its time advances by hand, so the frame-rate limit, the keepalive, the settling of a resize and the warning for an unserved panel are tested without a wall clock |

There is no stand-in for the QarOS SDK or for its C API. The code that calls the C API for panels, in the component `panels` of `qar_ogre_adapter`, has no automated test: only live sessions exercise it, and they ran with builds that were made before the last changes of the code. The check `--connect-only` of the example opens a session and creates no panel.

## What the automated tests do not cover

- **Calls to QarOS.** Every test of the library uses the recording sender, so the creation of the video sender and of the QarOS panel, the subscription for gestures and the real sequence of a close are exercised only in a live session. A widget is only a source of pixels for these tests; what a Visualizer shows of it is not tested.
- **AR input from a device.** The pointer samples are synthetic: they come from the queue that the tests fill, not from a headset.
- **Text.** The offscreen platform plugin, under which the tests run, rasterizes no glyphs, so a test cannot tell a changed text from an unchanged one. Where a test needs a change in the picture, it paints a known colour instead.
- **The desktop.** The offscreen plugin cannot show whether a window would reach a real desktop. The tests of the off-screen panels check the attributes and the flag that `show_offscreen` sets and use the visibility of the native window of Qt as the witness for popups; only a live run shows that nothing appears on the desktop of a running application.
- **JPEG.** Qt reads and writes JPEG through a plugin, and the test build copies that plugin next to the test executable, so the test that decodes a JPEG image runs. On a host whose Qt has no JPEG codec the test skips itself and reports the skip.

The desktop check of this library is a Level 2 procedure of [Verification](VERIFICATION.md#desktop-visualizer-checks-level-2) that was not run with the example of this repository. [What the live sessions showed](VERIFICATION.md#what-the-live-sessions-showed) says what the live sessions of qar_rviz_plugin showed of the library.

## The layering guard

The layering guard is a small Python program, `scripts\check_layering.py`, that reads the `#include` lines of the C++ sources and enforces the dependency table of the QarOS integration code: which module may include which. It exists because the structure of this stack (a core that calls no QarOS function, an adapter, and the GUI and ROS 2 layers above them) decays quietly when one convenient include crosses a layer. It needs only the Python standard library.

CTest registers one guard test per module that this repository holds, named `check_layering_<module>`, and the test `check_layering_selftest`, which proves that the guard still reports a violation of every rule. The guard checks every module of its table whose folder exists, because the table describes the whole family of repositories and a repository holds only some of the modules.

| Rule | What the guard enforces |
|---|---|
| L1, include classes | A module includes only the foreign header families listed for it: the standard library, Qt, Ogre, ROS 2, RViz, platform headers, GoogleTest, stb. |
| L2, project edges | A module includes only the project modules listed for it, and some edges only through named headers: no upward and no sideways edge. Every example folder has its own entry. |
| L3, vendored header | Only the wrapper header of the core includes the vendored QarOS C header. |
| L4, core calls no API | The core calls no QarOS C function; it uses the types of the C API only, so it never needs the SDK DLL. |
| L5, adapter components | Every file of the adapter belongs to one component and includes only headers of its own component and of the components it may use. |
| L6, loader | The dynamic-loading implementation macro of the C header appears exactly once, in the loader file of the adapter, and nothing defines the dynamic-loading switch except CMake. |

Comments and string literals are ignored; every `#include` line is classified.

### Run the guard by hand

The first command checks all modules of this repository and prints one summary line per module; the second runs the self-test. Both exit with 0 when everything is clean, with 1 when a rule is violated and with 2 when the arguments are wrong or no module folder exists.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --all
```
```text
check_layering examples: OK (1 files)
check_layering qar_qt_streaming: OK (54 files)
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --self-test
```
```text
check_layering self-test: OK
```

### Read a failure

For a clean module the guard prints the module name, the word OK and the number of files it read. For a module with violations it prints the number of violations and the number of files, and then one indented line per violation in this form: the file and line of the include, the rule that it breaks and what is not allowed there.

```text
path:line: [rule] detail
```

Fix the include rather than the table: move the code to the layer that may use that header, or pass the dependency in through an interface. Change the table in the script only when the architecture itself changes, and then change the documentation of the layers with it.

For this repository the guard checks two modules: `qar_qt_streaming` and `examples`. CTest registers one test for each of them, `check_layering_qar_qt_streaming` and `check_layering_examples`, and the test `check_layering_selftest`. What the rules mean here:

- `qar_qt_streaming` includes the standard library, Qt, its own headers and the headers of `qar_ar_core`, and nothing else: no Ogre, no ROS 2, no RViz and no platform header. Only the tests may include GoogleTest.
- Every folder under `examples` has its own entry. The folder `qt_panel_example` may include the standard library, Qt, `qar_qt_streaming`, `qar_ar_core` and the headers of the components `render` and `panels` of `qar_ogre_adapter`, and no header of another component.
- Only the wrapper header of `qar_ar_core` includes the vendored QarOS C header, so this repository never includes it directly.

## Adding a test

[Developing](DEVELOPING.md#adding-code-and-tests) explains where a new test goes, how to register it and how to name it.
