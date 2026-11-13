# Changelog

All notable changes to this repository are listed in this file, newest version first. The format follows Keep a Changelog, and the versions follow Semantic Versioning. While the major version is 0, the minor version marks changes that may not be backward compatible, and the CMake packages of this stack accept a request only for the same minor version.

Each version lists what it adds, the compatibility it was verified with (the QAROS version, the ROS 2 distribution, Qt, the toolchain and the versions of the repositories it builds on), and its known limitations. The version of a repository is the version of its CMake package, 0.2.0 for this release.

## Unreleased

### Changed

- the QarOS C header and the QarOS C++ SDK headers come from the public QarOS repository <https://github.com/Quaternar/qaros>, the git submodule `external/qaros` (pinned to `a507034` of `staging/main-with-headset`), through its CMake targets `qar-streaming-c-headers` and `qar-streaming-cpp`, so both always come from one QarOS revision; clone with `--recurse-submodules` or run `git submodule update --init`;
- the build no longer looks for the QarOS headers in the `include` folder of `QAR_SDK_ROOT`; `QAR_STREAMING_CPP_INCLUDE_DIR` still names a folder with both headers, and the new cache variable `QAR_QAROS_SOURCE_DIR` names another checkout of the QarOS repository.

### Removed

- the vendored copies `third_party/qar` and `third_party/qar_streaming_cpp`;
- the options `--hub`, `--code`, `--rejoin` and `--forget` of the example and the pairing it stored: they used the code onboarding, `qar_runtime_rejoin` and `qar_runtime_forget` (`Runtime::Rejoin`, `Runtime::Forget`), which QAROS deprecates. The example joins as a local app (`QarOnboardLocalAppExt`), the path QAROS recommends, and so joins only the Hub of the PC it runs on; `--local` is still accepted.

## 0.2.0

### Added

- a standalone package: `qar_qt_streaming` needs Qt and the headers of the QarOS C++ SDK, and nothing of ROS 2, RViz, Ogre or another repository of this stack; the CMake package has the components `seam`, `qar_qt_streaming` and `qaros`;
- Qt 5.15 and Qt 6.5 and newer from one source: the CMake variable `QAR_QT_MAJOR` selects the major version of Qt, the presets `vs2019-x64` and `vs2022-x64` build with Visual Studio 2019 and 2022, and a source ZIP of the repository builds as a subproject of another CMake project;
- the layer `seam` (target `qar_qt_streaming::seam`, standard library only): the value types of a panel (`Vector3`, `Quaternion`, `Pose`, `PanelSize`, `PeerId`, `PanelState`), the interfaces of the panel (`IPanelSender`, `IPanelSenderFactory`, `IPanelInputSource`, `PanelDiagnosticsSink`), the decision about a panel that QarOS already holds, the guard of the panel state, the naming rule of the common names, the timing log and the mapper of QarOS gestures to pointer samples (`PanelGestureMapper`);
- the layer `qaros` (target `qar_qt_streaming::qaros`, no Qt): the panel factory, panel sender and input source over the QarOS C++ SDK, which run every call that can wait on QarOS on a worker thread of their own and keep only the frame calls on the Qt thread;
- the example joins the QAROS Hub without a session owner: as a local app on the PC of the Hub (`--local`), with a pairing code from another PC (`--hub HOST:PORT --code CODE`, stored for the next start and used again by `--rejoin`, erased by `--forget`); `--invite` asks QAROS to start a target app after the join; Ctrl+C ends it in an orderly way, also while the join waits, and an end that finds the worker of the backend still busy after 8 s leaves QAROS unreleased and exits;
- support for Qt 6 in the input injector and the popup guard: a touch point carries the scene position and the screen position, `InputDeliveryMode::MouseOnly` serves an application that reads other positions of a touch point, the function `active_popup_widget()` names the popup that a panel keeps off the desktop, and the lists and menus that carry a graphics effect are drawn into the picture of a panel;
- the check of the macros `QAR_ENABLE_*` of the QarOS C header, which makes a program and the backend that were compiled with different values fail to link.

### Changed

- the interfaces and values that a program passes to the library are the types of `seam`: the interfaces of the panel sender, its factory and the input source are `IPanelSenderFactory`, `IPanelSender` and `IPanelInputSource`, their headers are `panel_sender.hpp` and `pointer_input.hpp`, and the values of the QarOS C header that crossed the library are `seam::Vector3`, `seam::Quaternion`, `seam::Pose`, `seam::PanelSize` and `seam::PanelState`;
- the value types of `seam` give `Quaternion::w` the value 1 and `PanelSize::content_scale` the value 1 when they are default constructed, which the structures of the QarOS C header do not;
- the private method `emit` of the pointer state machine is `append_action`, so that a header of the library parses after a Qt 6 header, where `emit` is a macro;
- the close reasons and the example texts that named RViz name the host instead, and the pairing texts of the example name no Hub card and no lifetime of the code.

### Removed

- the dependency on `qar_ogre_adapter`: the panel factory and the input source that the RViz plugin uses are part of the plugin repository `qar_rviz_plugin`, which depends on this one.

### Compatibility

| Item | Version |
|---|---|
| QAROS | the QAROS C API with onboarding (QAROS 0.9), verified against the installed QAROS 0.9.0-dev.3008+65dc9aeb |
| Qt | 5.15.2 (the kit `win64_msvc2019_64`), 6.5.3, 6.8.3 and 6.10.3 were built and tested as well, with the modules Core, Gui and Widgets |
| Compiler | Microsoft C++ toolset v142 of Visual Studio 2019; the toolset v143 of Visual Studio 2022 with Qt 6.8.3 and 6.10.3 |
| CMake | 3.22 or newer is required; verified with 3.29.5 as bundled with Visual Studio 2022 version 17.11; CMake 4 not tested |
| ROS 2, RViz, Ogre | not used |
| Python | 3.10.11; the layering guard needs only the Python standard library |
| GoogleTest | 1.14.0, downloaded when you configure, for the tests only |
| Other repositories | none |

### Known limitations

[SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations) lists what the QarOS SDK does not document or does not do, with the evidence for each item. These limits and those of Qt affect what you can build on this repository:

- No call of the SDK retires a logical video stream: a receiver that is attached to a destroyed sender keeps retrying until its peer goes away, so closing a panel does not stop that retry.
- A widget of a new size needs a new video sender, because the receiver of a panel takes its resolution from the layout of the sender; the library attaches the new sender to the same panel in the default mode `swap`, and makes a new panel in the mode `new-panel`.
- A cancelled gesture subscription may leave its runtime subscription until the session is destroyed.
- `QWidget::render()` cannot paint a native window that is embedded with `QWidget::createWindowContainer()`; that rectangle comes out solid. The library offers region sources, which paint a video frame over such a rectangle.
- The popup guard keeps the popups of an off-screen panel off the desktop, but not dialogs, windows of the type `Qt::Tool` or tooltips on Windows.
- Under the style `windows11` of Qt 6.8 the lists of combo boxes and menus are drawn into the picture of a panel without their drop shadow.
- The Qt example was run in a session with the desktop Visualizer with Qt 5.15 and Qt 6.8 (taps, a combo list, resizing, Ctrl+C, joining again without a second Allow); no session with a headset, no pairing with `--hub` and `--code`, no end with a worker that stays busy and no run with Qt 6.5 or Qt 6.10 has been made.
- The AR drag on Qt panels has been checked by unit tests and, through the RViz plugin `qar_rviz_plugin`, in a live session on streamed RViz dock panels (expanding a tree item, dragging a slider, scrolling), and not in the Qt example; the use of two hands has been checked by unit tests and not in a live session; the tap count and the long press have been checked by neither a unit test nor a live session.

## 0.1.0

### Added

The first version of the repository. It adds the features that the [README](README.md#features) describes:

- capture of any `QWidget` into a QarOS GUI panel with `QWidget::render()`: RGBA8888 pictures, a hash that finds unchanged pictures, a keepalive and a limit of 15 captures a second;
- sizing of panels for reading in AR: 1.2 mm for each logical pixel by default, and readability presets for 0.40 m, 0.65 m and 2.0 m with a render scale;
- sizing of panels for the frame that QarOS draws around the video: a panel is a fit margin taller than its video, the panel of a dialog fits its width to the frame, and the frame is `PanelFrame`;
- AR input as Qt events: touch with a mouse fallback, an implicit grab for each pointer, double clicks, drags, the hover lifecycle and two hands as two pointers;
- popups drawn into the picture of their panel, and dialogs as GUI panels of their own, with an optional fit of a dialog to its scroll areas;
- adoption of a new widget size only when it is large and has settled, and a new video sender on the same QarOS panel for it (`QAR_PANEL_RESIZE` chooses a second panel instead);
- stable common names for the panels (`name_scope`), so that a panel that was hidden when the session ended is found and shown again by the next start, and a session end that hides and detaches the panels in the modes `notice` and `park`;
- closing of the AR panel of a closed window, which destroys the video sender by default, with the close mode `notice` as the escape hatch and the close mode `park`, which closes nothing until the session ends;
- hiding and showing of panels in AR without closing them;
- the brightness of the frames that a panel sends and the census of the scroll areas of its widget, for the log;
- hidden off-screen panels, with a guard that keeps their popups off the desktop;
- video in panels: decoders for raw and compressed camera images, a one-slot mailbox for frames, a panel widget for video and region sources;
- the example `qar_qt_panel_example`, and unit tests that run under the offscreen platform plugin of Qt.

### Compatibility

| Item | Version |
|---|---|
| QAROS | verified against the installed QAROS 0.9.0-dev.3008+65dc9aeb |
| Qt | 5.15.2, the kit `win64_msvc2019_64`, with the modules Core, Gui and Widgets |
| Compiler | Microsoft C++ toolset v142 of Visual Studio 2019 |
| CMake | 3.22 or newer is required |
| GoogleTest | 1.14.0, downloaded when you configure, for the tests only |
