# Changelog

All notable changes to this repository are listed in this file, newest version first. The format follows Keep a Changelog, and the versions follow Semantic Versioning. While the major version is 0, the minor version marks changes that may not be backward compatible, and the CMake packages of this stack accept a request only for the same minor version.

Each version lists what it adds, the compatibility it was verified with (the QAROS version, the ROS 2 distribution, Qt, the toolchain and the versions of the repositories it builds on), and its known limitations. The version of a repository is the version of its CMake package, 0.1.0 for this release.

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
- the example joins the QAROS Hub through the session owner of `qar_ogre_adapter`, as a local app or with a pairing code;
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
| CMake | 3.22 or newer is required; verified with 3.29.5 as bundled with Visual Studio 2022 version 17.11; CMake 4 not tested |
| ROS 2 distribution | Humble, release `release-humble-20260220`; the library uses none of it, and `qar_ogre_adapter` and the example use its Ogre |
| Python | 3.10.11; the layering guard needs only the Python standard library |
| GoogleTest | 1.14.0, downloaded when you configure, for the tests only |
| Other repositories | `qar_ogre_adapter` 0.1.0 |

### Known limitations

[SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations) lists what the QarOS SDK does not document or does not do, with the evidence for each item. These limits and those of Qt affect what you can build on this repository:

- No call of the SDK retires a logical video stream: a receiver that is attached to a destroyed sender keeps retrying until its peer goes away, so closing a panel does not stop that retry.
- A widget of a new size needs a new video sender and a new panel, because the receiver of a panel takes its resolution from the layout of the sender.
- A cancelled gesture subscription may leave its runtime subscription until the session is destroyed.
- `QWidget::render()` cannot paint a native window that is embedded with `QWidget::createWindowContainer()`; that rectangle comes out solid. The library offers region sources, which paint a video frame over such a rectangle.
- The popup guard keeps the popups of an off-screen panel off the desktop, but not dialogs, windows of the type `Qt::Tool` or tooltips on Windows.
- The Qt example has not been run in a session with a Visualizer; the library ran in the live checks of qar_rviz_plugin on the desktop Visualizer, with a build that differs from this release only in comments and the text of one tool tip, and no session with a headset has run this release.
- The AR drag of a slider and the use of two hands on Qt panels have been checked by unit tests and not in a live session; the tap count and the long press have been checked by neither a unit test nor a live session.
