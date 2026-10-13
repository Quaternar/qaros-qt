# qar_qt_streaming - Qt widgets as QarOS AR GUI panels

qar_qt_streaming 0.1.0 turns Qt widgets into QarOS AR GUI panels. It draws a `QWidget` into a pixel buffer with the paint path of Qt, streams the picture to QarOS as the content of a GUI panel, and turns the gestures that QarOS reports on the panel into Qt events for the widget, so that a widget that works on the desktop works in AR. It is a C++17 static library for Windows 11 and MSVC that builds on Qt 5.15.2, delivered as the CMake package `qar_qt_streaming`, with an example, `qar_qt_panel_example`, that you can run.

## Who this is for

This repository is for QuaternAR engineers who integrate with QarOS. It assumes that you know QarOS and its C API, and it never explains them: where QarOS appears, the documentation states only what this stack relies on and how it uses the C API.

What it does explain, where it is used, is the part of the stack that is not QarOS:

- Qt 5.15.2 widgets and how a widget is captured off screen and fed with input;
- CMake and the Microsoft C++ toolchain, with the exact versions and the reason for each.

Everything that is not QarOS is installed from a fresh Windows 11 machine, step by step, in the installation document.

## Features

- **Any `QWidget` as a GUI panel.** The library draws the widget with `QWidget::render()` into an RGBA8888 buffer on the Qt GUI thread and sends a frame to QarOS only when the picture changed, which it finds by hashing the pixels. An unchanged picture is sent again after one second as a keepalive, and a panel is captured at most 15 times a second unless the host raises the limit.
- **Panels sized for reading in AR.** A panel hangs in the room at 1.2 mm for each logical pixel of the widget, whatever the pixel density of the display: a denser display makes the picture sharper and not larger. The helpers of `ar_readability.hpp` derive the size of a pixel and a render scale from a viewing distance, with presets for 0.40 m, 0.65 m and 2.0 m, and the render scale makes the buffer denser without changing the size of the panel.
- **AR input as Qt events.** A tap, a drag or a hover that QarOS reports on a panel reaches the widget as Qt events: touch events first, and mouse events for a widget that does not take touch, so that buttons, check boxes, sliders and combo boxes work. The library keeps an implicit grab for each pointer, recognises double clicks, sends the enter and leave events of a hover, and treats the two hands of a user as two pointers. The unit tests cover drags and two hands; no live session has exercised them yet.
- **Popups and dialogs.** The lists of the combo boxes, menus and completers that a panel opens are drawn into its picture where Qt placed them; a tooltip window is drawn only when it has the panel as its parent, and a tooltip of Qt on Windows has the desktop as its parent. A dialog that a panel opens becomes a GUI panel of its own, sized from the dialog and placed 0.5 m beside the panel that opened it, and it closes when the dialog hides.
- **Resizing without churn.** A widget of a new size needs a new video sender and a new panel, so the library adopts a new size only when it differs by at least 8 pixels and has held for half a second and for three ticks.
- **Closing a window closes its AR panel.** When a host reports a closed window, the manager closes the AR panel and destroys its video sender; with `QAR_PANEL_CLOSE_MODE` set to `notice`, the escape hatch for a runtime on which that is not safe, the panel stays and shows a notice. QuaternAR states that the current runtime survives the destruction of a stream.
- **Hidden off-screen panels.** `show_offscreen` shows a top-level widget to Qt, so that it lays out, paints and takes input, without a window ever reaching the desktop, and a guard keeps the popups of such a panel off the desktop too.
- **Video in panels.** `VideoPanelWidget` paints the newest frame of a `LatestVideoFrame`, the one-slot mailbox between a decoding thread and the GUI thread; `decode_raw_image` and `decode_compressed_image` turn raw and compressed camera images into the picture format of the capture; and a region source paints a video frame over a rectangle that `QWidget::render()` cannot paint, such as an embedded native window.
- **An example and tests.** `qar_qt_panel_example` streams a window and a hidden panel to a Visualizer, and the unit tests run under the offscreen platform plugin of Qt, so that they open no window.
- **Known limitations.** What the QarOS SDK does not document or does not do is listed in [Known SDK limitations](docs/SDK_COMPATIBILITY.md#known-sdk-limitations), and the symptoms of the limits that you may meet are in [Troubleshooting](docs/TROUBLESHOOTING.md#panel-problems).

## Where this fits

This repository is one of three that together stream an Ogre scene, Qt widgets, web pages and RViz into QarOS. Each repository has the version 0.1.0, and this documentation describes version 0.1.0 of all three: a repository asks for that version of the repositories it builds on, so the three have to be at matching versions. The table lists the three in the order in which they are installed: `qar_ogre_adapter` first, then `qar_qt_streaming`, then `qar_rviz_plugin`.

| Repository | Version | What it provides | Depends on | Also needs |
|---|---|---|---|---|
| qar_ogre_adapter | 0.1.0 | The QarOS integration layer: renders an Ogre scene for each eye and streams it to QarOS; the adapter for the QarOS C API; the vendored C header | none | The ROS 2 Humble distribution, for Ogre only |
| **qar_qt_streaming** (this repository) | 0.1.0 | Qt widgets as QarOS AR GUI panels | qar_ogre_adapter | Qt 5.15.2 |
| qar_rviz_plugin | 0.1.0 | The RViz2 plugin that streams RViz into QarOS, and web AR panels from a UI model and from the ROS 2 graph | qar_ogre_adapter, qar_qt_streaming | The ROS 2 Humble distribution with its repairs, Qt 5.15.2, colcon |

The installation document of each repository installs the repositories it depends on, with the commands for each, so you can start from any of them.

## Quick start

These ten steps take a Windows 11 machine that has the toolchain of [Installation](docs/INSTALL.md#toolchain) installed to a checked connection to QarOS. Every command is explained, with its output, in the document that the step names. Run the steps in one PowerShell 7 window, in this order.

1. Create the folders and set the seven variables that all commands use ([Directory layout and environment variables](docs/INSTALL.md#directory-layout-and-environment-variables)).

```powershell
$layout = [ordered]@{
    QAR_ROOT          = 'C:\qar'
    QAR_DEPS_ROOT     = 'C:\qar\deps'
    ROS2_INSTALL_ROOT = 'C:\qar\deps\ros2_humble\ros2-windows'
    QAR_QT_ROOT       = 'C:\qar\deps\Qt\5.15.2\msvc2019_64'
    QAR_VENV          = 'C:\qar\venv'
    QAR_PREFIX        = 'C:\qar\install'
    QAR_SDK_ROOT      = 'C:\QarOS\install'
}
foreach ($name in $layout.Keys) {
    [Environment]::SetEnvironmentVariable($name, $layout[$name], 'User')
    Set-Item -Path "Env:$name" -Value $layout[$name]
}
New-Item -ItemType Directory -Force -Path $layout.QAR_ROOT, $layout.QAR_DEPS_ROOT, $layout.QAR_PREFIX, (Split-Path $layout.QAR_SDK_ROOT) | Out-Null
```

> Success: the block prints nothing and ends without an error. Afterwards the seven variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps`, `C:\qar\install` and `C:\QarOS`.
>
> This command changes your machine, so no output is shown here.

2. Unpack the QarOS SDK drop that you received from QuaternAR, saved as `C:\QarOS\qaros_sdk.zip`, and remove the download mark from the unpacked programs ([The QarOS SDK](docs/INSTALL.md#the-qaros-sdk)).

```powershell
$sdkParent = Split-Path $env:QAR_SDK_ROOT
Expand-Archive -LiteralPath (Join-Path $sdkParent 'qaros_sdk.zip') -DestinationPath $sdkParent -Force
Get-ChildItem -LiteralPath $env:QAR_SDK_ROOT -Name
```
```text
bin
include
third_party_licenses.md
```

```powershell
Get-ChildItem -LiteralPath $env:QAR_SDK_ROOT -Recurse -Include *.exe, *.dll | Unblock-File
```

3. Clone this repository and go to the folder of the clone; the next steps run there until a step names another folder ([Python environment](docs/INSTALL.md#python-environment)).

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

4. Create the Python environment, which holds the installer of Qt, and fill it from the pinned list of the repository.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
...
Successfully installed PyYAML-6.0.3 Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 catkin-pkg-1.1.0 certifi-2026.6.17 charset-normalizer-3.4.9 colcon-cmake-0.2.30 colcon-common-extensions-0.3.0 colcon-core-0.21.0 colcon-defaults-0.2.9 colcon-devtools-0.3.0 colcon-library-path-0.2.1 colcon-metadata-0.2.5 colcon-notification-0.3.1 colcon-output-0.2.14 colcon-package-information-0.4.1 colcon-package-selection-0.2.10 colcon-parallel-executor-0.4.0 colcon-pkg-config-0.1.0 colcon-powershell-0.5.0 colcon-python-setup-py-0.2.9 colcon-recursive-crawl-0.2.3 colcon-ros-0.5.0 colcon-test-result-0.3.8 colorama-0.4.6 coloredlogs-15.0.1 coverage-7.15.0 defusedxml-0.7.1 distlib-0.4.3 docutils-0.23 empy-4.2.1 exceptiongroup-1.3.1 humanfriendly-10.0 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 lark-1.3.1 multivolumefile-0.2.3 netifaces-0.11.0 numpy-2.2.6 opencv-python-5.0.0.93 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyparsing-3.3.2 pyppmd-1.3.1 pyreadline3-3.5.6 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 python-dateutil-2.9.0.post0 pywin32-312 requests-2.34.2 semantic-version-2.10.0 six-1.17.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0 vcstool-0.3.0
```

5. Download and unpack the ROS 2 Humble distribution, which provides the Ogre that the upstream repository links, and install Qt ([ROS 2 Humble for Windows](docs/INSTALL.md#ros-2-humble-for-windows), [Qt 5.15.2](docs/INSTALL.md#qt-5152)). This repository needs none of the repairs that the reminder at the end of the output mentions: the repair scripts in the folder `scripts` serve qar_rviz_plugin.

```powershell
.\scripts\fetch_dependencies.ps1
```
```text
=== ROS 2 Humble Windows binary distro ===
Downloading https://github.com/ros2/ros2/releases/download/release-humble-20260220/ros2-humble-20260220-windows-release-amd64.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-humble-20260220-windows-release-amd64.zip
Extracting to C:\qar\deps\ros2_humble

=== ROS 2 source manifest repo (reference only) ===
Downloading https://github.com/ros2/ros2/archive/refs/tags/release-humble-20260220.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-release-humble-20260220-src.zip
Extracting to C:\qar\deps\ros2_src_release-humble-20260220

Done. Binary distro: C:\qar\deps\ros2_humble\ros2-windows
Reminder: the binary distro has packaging gaps - run the repair scripts next:
fix_missing_spdlog_dll.ps1, fix_missing_vendor_dlls.ps1, fix_missing_openssl_dll.ps1, fix_qt_conda_imports.ps1.
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir "$env:QAR_DEPS_ROOT\Qt"
```
```text
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.10.11 [CPython MSC v.1929 64 bit (AMD64)]
...
INFO    : Patching C:\qar\deps\Qt\5.15.2\msvc2019_64\bin\qmake.exe
...
INFO    : Finished installation
INFO    : Time elapsed: 15.21180160 second
```

6. Clone the upstream repository `qar_ogre_adapter`, go to the folder of its clone and configure it ([Upstream repositories](docs/INSTALL.md#upstream-repositories)).

```powershell
git clone https://github.com/Quaternar/qaros-ogre "$env:QAR_ROOT\src\qar_ogre_adapter"
```
```text
Cloning into 'C:\qar\src\qar_ogre_adapter'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_ogre_adapter"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
  QAR_SDK_ROOT="C:\QarOS\install"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
-- Found rviz_ogre_vendor: 11.2.25 (C:/qar/deps/ros2_humble/ros2-windows/share/rviz_ogre_vendor/cmake)
...
-- qar_qar_adapter: QarOS SDK backend, DYNAMIC loading of C:\QarOS\install/bin/qar-streaming-c.dll (no import lib linked)
...
-- Configuring done (7.7s)
-- Generating done (0.1s)
-- Build files have been written to: C:/qar/src/qar_ogre_adapter/build/vs2019-x64
```

7. Build the upstream repository and install it into `QAR_PREFIX`.

```powershell
cmake --build --preset vs2019-x64
```
```text
...
  qar_ar_core.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ar_core\RelWithDebInfo\qar_ar_core.lib
...
  qar_ogre_streaming.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ogre_streaming\RelWithDebInfo\qar_ogre_streaming.lib
...
  qar_qar_adapter_render.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_render.lib
...
  qar_ogre_standalone_streaming_sample.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\ogre_standalone_streaming_sample\RelWithDebInfo\qar_ogre_standalone_streaming_sample.exe
...
  qar_ogre_live_stream_example.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\qaros_live_stream\RelWithDebInfo\qar_ogre_live_stream_example.exe
...
```

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_ar_core.lib
-- Installing: C:\qar\install/lib/qar_ogre_streaming.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_capi.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_render.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_panels.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_url.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_gestures.lib
-- Installing: C:\qar\install/include
...
-- Installing: C:\qar\install/include/qar_streaming.h
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE.qar_streaming_header
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfig.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfigVersion.cmake
```

8. Go back to the folder of the clone of this repository, then configure and build this repository, which finds the upstream package in `QAR_PREFIX` ([Build and install qar_qt_streaming](docs/INSTALL.md#build-and-install-qar_qt_streaming)).

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
...
-- Configuring done (8.0s)
-- Generating done (0.1s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
...
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
...
```

9. Run the tests ([Testing](docs/TESTING.md)).

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_qt_streaming/build/vs2019-x64
        Start   1: check_layering_qar_qt_streaming
  1/420 Test   #1: check_layering_qar_qt_streaming ...........................................................   Passed    0.15 sec
        Start   2: check_layering_examples
  2/420 Test   #2: check_layering_examples ...................................................................   Passed    0.04 sec
        Start   3: check_layering_selftest
  3/420 Test   #3: check_layering_selftest ...................................................................   Passed    0.05 sec
        Start   4: AlignUp.RoundsUpToTheNextMultiple
  4/420 Test   #4: AlignUp.RoundsUpToTheNextMultiple .........................................................   Passed    0.01 sec
...
100% tests passed, 0 tests failed out of 420

Total Test time (real) =  30.97 sec
```

10. Check the connection to QarOS. The example opens a QarOS session and exits; it opens no window and invites nobody. The step needs the SDK; without it, the same program started with `--help` shows its options ([Check the connection to QarOS without a window](docs/GETTING_STARTED.md#check-the-connection-to-qaros-without-a-window)).

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe --sdk-bin "$env:QAR_SDK_ROOT\bin" --connect-only
```
```text
[qar-adapter] library logging: severity=warn console=off folder=C:\Users\<user>\AppData\Local\Temp\quaternar
[qar-adapter] zenoh logging: off (QAR_ZENOH_LOG not set)
example: connected to QarOS: ready=1
```

[Getting started](docs/GETTING_STARTED.md) goes on from here: it shows how AR input and closing work and lists what to check when you put the library into your own Qt application. The live example that streams a window and a hidden panel to a Visualizer is [Stream one widget into AR](docs/GETTING_STARTED.md#stream-one-widget-into-ar); it needs a drop of the QarOS SDK, which holds the Visualizer, and QarOS starts that Visualizer itself from the `bin` folder of the drop.

## Documentation

| Document | What it covers |
|---|---|
| [Installation](docs/INSTALL.md) | The machine, the toolchain, the dependencies and this repository, step by step, with the commands |
| [Getting started](docs/GETTING_STARTED.md) | The first program that you run, and what you see |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured, which part owns what, and why |
| [API](docs/API.md) | The public interface, with examples that are compiled with the repository |
| [Configuration](docs/CONFIGURATION.md) | Every environment variable and build option |
| [Testing](docs/TESTING.md) | How to run the tests, what they cover and what they do not |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Symptoms, causes and fixes |
| [SDK compatibility](docs/SDK_COMPATIBILITY.md) | The supported QarOS SDK drops and the QarOS behaviour this repository relies on |
| [Developing](docs/DEVELOPING.md) | The layout of the repository and the conventions for changing it |
| [Verification](docs/VERIFICATION.md) | The checks of each level, how to run them, and their status |
| [Changelog](CHANGELOG.md) | What each version contains |
| [License](LICENSE) | The license of this repository |

## The QarOS SDK is not included

The QarOS runtime, which consists of `qar-streaming-c.dll`, `qar_streaming.dll`, the zenoh and GStreamer stacks and the Visualizer `qar-streaming-viz.exe`, is QuaternAR's proprietary binary distribution and is not part of this repository. You obtain a drop of it from QuaternAR. The crash reports of the Visualizer carry the name of a second program of the same SDK folder, `qar-stereokit-gui.exe`.

No part of the SDK is copied into this repository. The C header of the QarOS C API is vendored by the repository qar_ogre_adapter and installed with its libraries.

You do not need the SDK to build this repository or to run its tests. Nothing links against the SDK: the library `qar-streaming-c.dll` is loaded at run time from the folder that a program passes in, and configuring this repository does not look at the SDK. You need the SDK to connect to QarOS.

Point a program at the SDK by passing the `bin` folder of the SDK in the `runtime_binaries_folder_path` field of the QarOS session factory configuration.

This stack was verified against the QarOS SDK drop of 2026-09-28 and needs at least the drop of 2026-09-21; the [SDK compatibility](docs/SDK_COMPATIBILITY.md) document says how to identify the drop you have and what changed between drops. At the time of writing, the public QarOS repository <https://github.com/Quaternar/qaros> publishes a later generation of the C API. This stack does not build against it: the session set-up (create, serialise, join and invite), the hand of a gesture, the time points and the init structures of volumes and panels would have to be ported.

## Platform support

The stack runs on Windows 11, 64-bit, and is built with the Microsoft C++ toolset v142 (Visual Studio 2019). It is run from a local desktop session. Linux is not built or tested.

## License

This repository is licensed under the MIT license; the text is in [LICENSE](LICENSE), and every C++ source and header carries the identifier of the license and the copyright line of Quaternar.

Third-party material in this repository:

- This repository contains no third-party source files.
- GoogleTest is downloaded when you configure, only for the tests; it is not part of this repository and is not installed with it.
