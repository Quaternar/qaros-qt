# Developing

## Repository layout

The code of this repository is one library, in a folder of its own, plus one example, the documentation and the build files. [Components](ARCHITECTURE.md#components) shows what is in the library.

| Path | What it holds |
|---|---|
| `qar_qt_streaming/` | the library: headers in `qar_qt_streaming/include/qar_qt_streaming/`, sources in `qar_qt_streaming/src/`, tests in `qar_qt_streaming/test/`, and the header-only test doubles in `qar_qt_streaming/test_support/` |
| `examples/qt_panel_example/` | the example that streams a window and a hidden panel to a QarOS Visualizer |
| `scripts/` | the layering guard `check_layering.py`, `fetch_dependencies.ps1`, which [Installation](INSTALL.md) runs, and `qar_locations.ps1`, which the PowerShell scripts share; the repair scripts whose names start with fix_ and `env_setup.ps1` serve qar_rviz_plugin, and this repository needs none of them |
| `cmake/` | `QarBuildSupport.cmake`, the build services that the top-level project calls |
| `docs/` | these documents, `docs/snippets/` with the compile-checked snippets, and `docs/snippets/consumer/`, the install smoke test |

Every header in the `include` folder is public. The folder `src` holds sources only, so a consumer cannot include anything by accident that is meant to be private.

## Layering rules

The layering guard, `scripts/check_layering.py`, enforces the rules of this section ([The layering guard](TESTING.md#the-layering-guard) shows how to run it). The table lists, for each folder, the project modules and the kinds of foreign headers that its production code may include; the tests of the library may add GoogleTest.

| Folder | Project modules it may include | Foreign headers it may include |
|---|---|---|
| `qar_qt_streaming` | itself and `qar_ar_core` | the standard library and Qt |
| `examples/qt_panel_example` | `qar_qt_streaming`, `qar_ar_core`, and the headers of the components `render` and `panels` of `qar_qar_adapter` | the standard library and Qt |

The module may therefore not include Ogre, RViz, ROS, a platform header, the vendored QarOS header or anything of the adapter beyond the seams that live in `qar_ar_core`, and the only header that includes the vendored QarOS header is the one wrapper header of `qar_ar_core`. The reason is the purpose of the library: Qt is allowed because it is the point of the library, and everything else is forbidden so that any Qt application can use it, with no dependency on the framework of a host. The boundary is also what keeps the seams free of Qt types.

## Build conventions

- **Language level and warnings.** The library requires C++17 as a compile feature, and the warning flags `/W4 /permissive-` come from `qar_apply_warning_flags`, which the library, its tests and the snippets call.
- **A static library.** The library is a static library with position-independent code, and consumers link the alias `qar_qt_streaming::qar_qt_streaming`; the test doubles are the alias `qar_qt_streaming::test_support`.
- **Qt is a public dependency.** The public headers take `QWidget` pointers and `QtWidgetCapture` holds a `QImage` by value, so Qt Core, Gui and Widgets are linked publicly, and so is `qar_ar_core`, whose seam types appear in the public headers.
- **Build type.** Build `RelWithDebInfo`: a single-configuration generator defaults to it, and the reason is the release runtime of the ROS 2 and Qt binaries, which a debug runtime in the same process cannot share.
- **Tests.** GoogleTest 1.14.0 is downloaded at configure time and checked against a hash, one executable, `qar_qt_streaming_tests`, holds all the tests, and each test is registered with CTest through `gtest_discover_tests`.
- **The Qt runtime next to the tests.** `gtest_discover_tests` runs the executable to list the cases, before any environment of a test can apply, so the build copies the Qt DLLs and the offscreen platform plugin next to the executable, and a plain shell can run and list the tests.

The options and the variables of the configure step are in [CMake options and cache variables](CONFIGURATION.md#cmake-options-and-cache-variables).

## Coding conventions

These conventions apply to every C++ file of this repository. Keep them when you add code; the layering guard and the tests catch some violations, the rest is up to review.

**Language level and warnings.** The code is C++17. Targets are compiled with the warning level `/W4`, and with `/permissive-` where the module sets it.

**Configuration.** Build `RelWithDebInfo`, never Debug. The ROS 2 and Qt binaries that this code links or loads are release builds, and a debug C runtime in the same process crashes on load.

**File header.** Every C++ source and header starts with these two lines, and the year and owner are the same in every file:

```text
// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
```

**Names.** The namespace of a module is the name of the module. Header guards follow the ROS 2 style, the module name in capitals, two underscores, the file name in capitals and a trailing underscore, for example `QAR_QAR_ADAPTER__ENV_CONFIG_HPP_` for the file `env_config.hpp` of the module `qar_qar_adapter`.

**Comments and documentation.** Write in English. A comment states what the code does and why it does it that way, as the code is now. It contains no dates, no milestone or ticket tags, no names of people or tools and no account of how the code came to be. Documentation comments describe the contract of a function or type: what it expects, what it returns, who owns what, and on which thread it may be called.

**Logic that talks to another system.** Code that reads the environment, Qt, ROS 2 or QarOS is kept thin; the decisions it makes are put into functions that take plain values, so that they can be tested without a QarOS session, a window or a ROS 2 graph. For example, the helpers that parse environment variables take the raw text and never read the environment themselves; their callers pass it in. There is no fake of the QarOS SDK.

Three rules of the threads apply to the code of this repository. Everything that touches a widget runs on the Qt GUI thread, and so do the synchronous calls of the C API that create and close a panel. Everything that another thread may do is in the mailbox of the video frames, which holds one mutex, and in the decoders, which touch only `QImage`. And the library starts no thread of its own.

## Adding code and tests

- **A new source file** goes into `qar_qt_streaming/src/` and its header into `qar_qt_streaming/include/qar_qt_streaming/`. List the source in the `add_library` call of `qar_qt_streaming/CMakeLists.txt`; the headers need no entry.
- **A new test** goes into `qar_qt_streaming/test/` and is listed in `qar_qt_streaming/test/CMakeLists.txt`. A case has the form `TEST(Suite, Name)`, and the name says what the case asserts. The tests run under the offscreen platform plugin of Qt: `test_main.cpp` creates the `QApplication` before any widget and sets `QT_QPA_PLATFORM` to `offscreen` when the caller chose no platform, so no window is ever made. The tests drive the real paint path of Qt, because a stub of `QWidget::render()` would test nothing.
- **A decision that depends on Qt, QarOS or the environment** goes into a function over plain values, which the test calls without a widget or a session. The library has such units: the geometry, the readability, the hit test, the pointer state machine, the capture gate and the parsers of the close mode and of the off-screen size, which take the text from the host and never read the environment.
- **A double of the QarOS side** is the recording sender, the queued input source and the manual clock of `test_support`, as [Test doubles](TESTING.md#test-doubles) describes. There is no fake of the QarOS SDK: code that calls the C API has no automated test and is exercised only against the real runtime, by the check `--connect-only` for the bootstrap of a session and by live sessions for the rest.
- **After a change that touches an include,** run the layering guard. A new module and a new example folder each need an entry in the tables of `check_layering.py`: `MODULES`, `PROJECT_ROOTS` and `POLICY`, and `EXAMPLE_POLICY`.

## Documentation and snippets

Every block of C++ or CMake in these documents is a copy of a region of a compiled file. The regions are in the files `docs/snippets/*.cpp` and `docs/snippets/CMakeLists.txt` (the target `qar_qt_streaming_doc_snippets`), in `docs/snippets/consumer/CMakeLists.txt`, and in the example `examples/qt_panel_example/main.cpp` and its `CMakeLists.txt`. A region begins at a comment line that holds the word qar-snippet and the name of the region in square brackets, and it ends at a comment line with qar-snippet-end and the same name. The snippet target is built with the repository and never run: the build proves that the code of the documents compiles and links against the current headers.

When you change a signature, build and fix the regions that no longer compile; then update the block of the document by hand, because nothing compares the Markdown with the region. To find the region of a block, search the text of the block in `docs/snippets` and in `examples`. Every snippet file includes the headers of this library before the headers of Qt, for the reason that [Qt rules](#qt-rules) gives. The consumer project `docs/snippets/consumer` is not part of the build of this repository: it needs the installed package, and [Install smoke test](INSTALL.md#install-smoke-test) configures it on its own.

## Qt rules

- **The GUI thread only.** Every method of the manager, of a stream, of the capture and of the injector is called on the thread that owns the widgets. `QWidget::render()` and the delivery of events into a widget are legal nowhere else, and the injector refuses a call from another thread and counts it.
- **Never run input from the handler of the tick timer.** Delivering an action runs arbitrary widget code, which may spin a nested event loop that stops a timer whose handler is on the stack. A host sets `pump_input_in_tick` to false and posts the call of `pump_input()`; [Popups and dialogs](ARCHITECTURE.md#popups-and-dialogs) has the reason. Code that you add to the delivery path must also stay re-entrant.
- **Hold a widget that you do not own through a `QPointer`.** The injector, the dialog sweep, the region sources and the attribution all hold the widgets that can die between two ticks through `QPointer`, so that a destroyed widget drops an action or a panel and never crashes. The capture and a stream hold the widget that the host gave them as a plain borrowed pointer, which has to outlive them: close the panel before you delete the widget.
- **Include order and `emit`.** `panel_pointer_state.hpp` declares a method named `emit`, which only parses while the macro of that name does not exist. Keep the Qt-free headers of this library above every header that includes a Qt header, in your own files and in new headers of the library, or compile with `QT_NO_KEYWORDS`.
- **No Qt type in a seam that `qar_ar_core` shares.** The seams are plain structs and interfaces, so that qar_ogre_adapter implements them without Qt; a new field of the seam holds a C++ standard type or a type of the QarOS header.
- **A window that must not reach the desktop is `Qt::WA_DontShowOnScreen`.** The notice, the video panel and the off-screen panel are all parentless, carry the attribute and are shown, so that Qt treats them as visible, and none of them opens a window on the desktop. Never move or resize a window of the host to make a capture easier: the library keeps the picture and the hit test in step by clipping and by giving a dialog a panel of its own, and not by rearranging desktop windows.
