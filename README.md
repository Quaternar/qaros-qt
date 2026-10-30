# qar_qt_streaming - Qt widgets as QarOS AR GUI panels

qar_qt_streaming 0.1.0 turns Qt widgets into QarOS AR GUI panels. It draws a `QWidget` into a pixel buffer with the paint path of Qt, streams the picture to QarOS as the content of a GUI panel, and turns the gestures that QarOS reports on the panel into Qt events for the widget, so that a widget that works on the desktop works in AR. It is a C++17 static library for Windows 11 and MSVC that builds on Qt 5.15.2, delivered as the CMake package `qar_qt_streaming`, with an example, `qar_qt_panel_example`, that you can run.

## TL;DR

You need Windows 11, PowerShell 7, the toolchain, Qt, the ROS 2 distribution that provides Ogre (on Windows, the Humble binary distribution: [Installation](docs/INSTALL.md#ros-2-humble-for-windows)), the layout variables and the installed `qar_ogre_adapter` of [Installation](docs/INSTALL.md); the last step also needs QAROS installed ([The QarOS SDK](docs/INSTALL.md#the-qaros-sdk)). Then clone, build, test, install and run:

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
cmake --preset vs2019-x64
cmake --build --preset vs2019-x64
ctest --preset vs2019-x64
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```

The tests end with the line 100% tests passed.

To use QAROS, run the example. It joins the QAROS Hub of this PC as a source app: QAROS asks to allow the program once for each build of it (and again after a rebuild), so click Allow in the QAROS window. Then open a Visualizer from QAROS and add a receiver in it if it has none, and the example streams a window and a hidden panel to it. A Hub on another PC needs `--hub HOST:PORT --code CODE` instead of `--local`.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$env:QAR_QT_ROOT\plugins\platforms"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --local --seconds 60
```

The ten steps from a clean machine, with every output, are in [Quick start](docs/QUICKSTART.md); the details of each step are in the documents of the [Documentation](#documentation) table, and the web documentation at <https://docs.quaternar.com/> describes the stack in the context of QAROS: [Ogre and Qt in RViz](https://docs.quaternar.com/docs/next/developer-guide/ogre-qt-rviz/), [Qt AR panels](https://docs.quaternar.com/docs/next/developer-guide/qt-ar-panels/) and [SDK limits](https://docs.quaternar.com/docs/next/developer-guide/sdk-limits/).

## Who this is for

This repository is for QuaternAR engineers who integrate with QarOS. It assumes that you know QarOS and its C API, and it never explains them: where QarOS appears, the documentation states only what this stack relies on and how it uses the C API.

What it does explain, where it is used, is the part of the stack that is not QarOS:

- Qt 5.15.2 widgets and how a widget is captured off screen and fed with input;
- CMake and the Microsoft C++ toolchain, with the exact versions and the reason for each.

Everything that is not QarOS is installed from a fresh Windows 11 machine, step by step, in the installation document.

## Features

- **Any `QWidget` as a GUI panel.** The library draws the widget with `QWidget::render()` into an RGBA8888 buffer on the Qt GUI thread and sends a frame to QarOS only when the picture changed.
- **Panels sized for reading in AR.** A panel hangs in the room at 1.2 mm for each logical pixel of the widget, whatever the pixel density of the display, and the helpers of `ar_readability.hpp` derive a size and a render scale from a viewing distance.
- **AR input as Qt events.** A tap, a drag or a hover that QarOS reports on a panel reaches the widget as Qt events, so that buttons, check boxes, sliders and combo boxes work; the unit tests cover drags and two hands, and no live session has run them.
- **Popups, dialogs and resizing.** The popups of a panel are drawn into its picture, a dialog that a panel opens becomes a GUI panel of its own, and a widget of a new size gets a new video sender on the same QarOS panel.
- **Closing, hiding and ending the session.** A closed window closes its AR panel, or with `QAR_PANEL_CLOSE_MODE` set to `notice` or `park` keeps it; `set_panel_ar_visible` hides and shows a panel without closing it; panels that are kept at the end of a session are hidden, and the next start finds each one again by its common name.
- **Off-screen panels and video.** `show_offscreen` shows a top-level widget to Qt without a window ever reaching the desktop, and `VideoPanelWidget` paints the newest frame of a decoded video.
- **An example and tests.** `qar_qt_panel_example` joins the QAROS Hub and streams a window and a hidden panel to a target, and the unit tests run under the offscreen platform plugin of Qt, so that they open no window.
- **Known limitations.** What the QarOS SDK does not document or does not do is listed in [Known SDK limitations](docs/SDK_COMPATIBILITY.md#known-sdk-limitations), and the symptoms of the limits that you may meet are in [Troubleshooting](docs/TROUBLESHOOTING.md#panel-problems).

Each feature is described in [Feature details](docs/ARCHITECTURE.md#feature-details).

## Where this fits

This repository is one of three that together stream an Ogre scene, Qt widgets, web pages and RViz into QarOS. Each repository has the version 0.1.0, and this documentation describes version 0.1.0 of all three: a repository asks for that version of the repositories it builds on, so the three have to be at matching versions. The table lists the three in the order in which they are installed: `qar_ogre_adapter` first, then `qar_qt_streaming`, then `qar_rviz_plugin`.

| Repository | Version | What it provides | Depends on | Also needs |
|---|---|---|---|---|
| qar_ogre_adapter | 0.1.0 | The QarOS integration layer: renders an Ogre scene for each eye and streams it to QarOS; the adapter for the QarOS C API; the vendored C header | none | The ROS 2 distribution, for Ogre only |
| **qar_qt_streaming** (this repository) | 0.1.0 | Qt widgets as QarOS AR GUI panels | qar_ogre_adapter | Qt 5.15.2 |
| qar_rviz_plugin | 0.1.0 | The RViz2 plugin that streams RViz into QarOS, and web AR panels from a UI model and from the ROS 2 graph | qar_ogre_adapter, qar_qt_streaming | The ROS 2 distribution with its repairs, Qt 5.15.2, colcon |

The installation document of each repository installs the repositories it depends on, with the commands for each, so you can start from any of them.

## Documentation

| Document | What it covers |
|---|---|
| [Quick start](docs/QUICKSTART.md) | The steps from a clean machine to a running example, with the output of each |
| [Installation](docs/INSTALL.md) | The machine, the toolchain, the dependencies and this repository, step by step, with the commands |
| [Getting started](docs/GETTING_STARTED.md) | The first program that you run, and what you see |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured, which part owns what, and why |
| [API](docs/API.md) | The public interface, with examples that are compiled with the repository |
| [Configuration](docs/CONFIGURATION.md) | Every environment variable and build option |
| [Testing](docs/TESTING.md) | How to run the tests, what they cover and what they do not |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Symptoms, causes and fixes |
| [SDK compatibility](docs/SDK_COMPATIBILITY.md) | The QAROS version that was verified and the QarOS behaviour this repository relies on |
| [Developing](docs/DEVELOPING.md) | The layout of the repository and the conventions for changing it |
| [Verification](docs/VERIFICATION.md) | The checks of each level, how to run them, and their status |
| [Changelog](CHANGELOG.md) | What each version contains |
| [License](LICENSE) | The license of this repository |

## The QarOS SDK is not included

QAROS, which consists of `qar-streaming-c.dll`, `qar_streaming.dll`, the zenoh and GStreamer stacks, the Hub and the Visualizer `qar-streaming-viz.exe`, is QuaternAR's proprietary product and is not part of this repository. You obtain its setup program from QuaternAR and install it once on the PC. The crash reports of the Visualizer carry the name of a second program of the same installation, `qar-stereokit-gui.exe`.

No part of QAROS is copied into this repository. The C header of the QarOS C API is vendored by the repository qar_ogre_adapter and installed with its libraries.

You do not need QAROS to build this repository or to run its tests. Nothing links against QAROS: a program loads the installed library `qar-streaming-c.dll` at run time, and configuring this repository does not look at QAROS. You need QAROS to connect to it.

A program finds the installed QAROS by itself: the installer records the install folder in the registry, and the library is loaded from there. How a program joins the Hub is described on the pages of QuaternAR: [Onboarding and sessions](https://docs.quaternar.com/docs/next/developer-guide/onboarding-and-sessions/).

This stack was verified against the installed QAROS 0.9.0-dev.3008+65dc9aeb; the [SDK compatibility](docs/SDK_COMPATIBILITY.md) document says how to identify the version you have.

## Platform support

The stack runs on Windows 11, 64-bit, and is built with the Microsoft C++ toolset v142 (Visual Studio 2019). It is run from a local desktop session. Linux is not built or tested.

## License

This repository is licensed under the MIT license; the text is in [LICENSE](LICENSE), and every C++ source and header carries the identifier of the license and the copyright line of QuaternAR.

Third-party material in this repository:

- This repository contains no third-party source files.
- GoogleTest is downloaded when you configure, only for the tests; it is not part of this repository and is not installed with it.
