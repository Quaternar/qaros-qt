# Changelog

What changes for users of qar_qt_streaming: the CMake package, its components and headers, the
example and what it shows. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
with a **Breaking** section first, and versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html):
a minor version adds features (anything that breaks a user is listed under Breaking with its
migration), a patch version only fixes. The version is the file [VERSION](VERSION); how entries are
written and how a version is released is in [AGENTS.md](AGENTS.md).

## [Unreleased]

### Breaking

- The QarOS C header and C++ SDK headers come from the public QarOS repository, the git submodule `external/qaros`, through its targets `qar-streaming-c-headers` and `qar-streaming-cpp`: clone with `--recurse-submodules` or run `git submodule update --init` ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- The build no longer looks for the QarOS headers in the `include` folder of `QAR_SDK_ROOT`; `QAR_STREAMING_CPP_INCLUDE_DIR` still names a folder with both headers, and the new cache variable `QAR_QAROS_SOURCE_DIR` names another QarOS checkout ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).

### Features

- The version of a build comes from the file `VERSION`, the last release, and git tags (`X.Y.Z` on a release tag, the next patch `X.Y.(Z+1)-dev.N+<sha>` on any other commit); `qar_qt_streaming/seam/version.hpp` reports it as `QAR_QT_STREAMING_VERSION` and `qar_qt_streaming::kLibraryVersion`, the example prints it with `--version` and tells it to the Hub as its app version ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- The resize mode `defer` (`QAR_PANEL_RESIZE=defer`) keeps the size of the sender of a panel that is visible in a QarOS session and adopts a settled size while the panel is hidden.
- A size change is adopted only when no mouse button is held (`resize_wait_for_mouse_release`) and the last rebuild of the sender is at least `resize_min_swap_interval_seconds`, 10 seconds, old.
- `active_popup_for` gives the injector the popup that belongs to a panel and never a popup of the host application, so that AR input cannot move the highlight of a menu of the host or close it.
- `abandoned_workers()` of the backend returns the number of workers of the process that were left running at the end of a factory, for a host that releases nothing of QarOS under such a worker.
- `QtPanelStream::set_scale()` and `QtPanelStreamManager::set_panel_scale()` change the metres that one logical pixel covers and the supersampling of one stream, with a placement sent once for a change of the metres alone.
- `EnlargedControlsStyle` draws the controls of a widget tree a factor larger and keeps the text; `apply_style_to_tree`, `remove_style_from_tree` and `enlarged_controls_style_of` go with it.
- `WidgetTwinBinder` keeps a second widget tree showing the state of the first and carries the input of the second out on the first.
- `set_dialog_scale_provider()` and `set_dialog_scale_reporter()` of the manager let a host give the panel of a dialog a scale of its own and learn what the panel got; the bounds `kMaxMetersPerLogicalPx`, `kMinRenderScale` and `kMaxRenderScale` and `scale_within_limits()` go with them.
- `IPanelSender::ensure_input()` is called by the stream for a kept panel that is shown again, so that it takes AR input again, and `ReplacedPanelFate::Revived` with the flag `revived` appears in the report of a size change.

### Enhancements

- A call of a deprecated API is a compile error in this repository's targets (`/we4996`, `-Werror=deprecated-declarations` with other compilers) ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- `qar_qt_streaming::qaros` no longer passes `/permissive-` to the programs that link it ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- The example joins with the C++ SDK builder `OnboardBuilder::WithLocalApp` instead of the C API; `local_app_onboarding.cpp` and `local_app_onboarding.hpp` are gone ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- In the resize mode `new-panel` the old panels of a stream are kept as a ring of at most `max_replaced_panels` hidden panels, and with the ring full the sender of the next size is made on the oldest hidden panel: no QarOS panel is closed while the session runs, in every close mode.
- The step of a dialog panel from its owner grows with the larger of the metres per logical pixel of the owner and of the dialog, so the dialogs of a scaled panel do not overlap it.
- A size has to hold for 1.5 seconds (`resize_settle_seconds`) before it is adopted.

### Removed

- The vendored copies `third_party/qar` and `third_party/qar_streaming_cpp` ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).
- The example options `--hub`, `--code`, `--rejoin` and `--forget` and the pairing it stored: the example joins only the Hub of its own PC as a local app; `--local` is still accepted ([#5](https://github.com/Quaternar/qaros-qt/pull/5)).

## [0.2.0] - 2026-11-13

Verified with QAROS 0.9.0-dev.3008+65dc9aeb, Qt 5.15.2, 6.5.3, 6.8.3 and 6.10.3, MSVC v142 and v143 and CMake 3.22 or newer; not tagged. Known limitations: [SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations).

### Breaking

- The interfaces and values a program passes are the types of `seam`: `IPanelSenderFactory`, `IPanelSender` and `IPanelInputSource` in `panel_sender.hpp` and `pointer_input.hpp`, and `seam::Vector3`, `Quaternion`, `Pose`, `PanelSize` and `PanelState` instead of the QarOS C structures ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- Default-constructed `seam::Quaternion` has `w = 1` and `seam::PanelSize` has `content_scale = 1`, unlike the QarOS C structures ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- The dependency on `qar_ogre_adapter` is gone: the panel factory and input source the RViz plugin uses moved to qaros-ros-rviz ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).

### Features

- Standalone package with the components `seam`, `qar_qt_streaming` and `qaros`: needs Qt and the QarOS C++ SDK headers, nothing of ROS 2, RViz or Ogre ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- Qt 5.15 and Qt 6.5 or newer from one source (`QAR_QT_MAJOR`), the presets `vs2019-x64` and `vs2022-x64`, and a source ZIP that builds as a subproject ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- The standard-library layer `seam`: panel values and interfaces, the attach decision, the panel state guard, the naming rule, the timing log and `PanelGestureMapper` ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- The backend `qaros` over the QarOS C++ SDK, which runs every call that can wait on QarOS on its own worker thread ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- The example joins the QAROS Hub without a session owner, invites a target app with `--invite` and ends in order on Ctrl+C ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- Qt 6 support in the input injector and the popup guard, `InputDeliveryMode::MouseOnly` and `active_popup_widget()` ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).
- A link-time check that a program and the backend were compiled with the same `QAR_ENABLE_*` macros of the QarOS C header ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).

### Enhancements

- Close reasons and example texts name the host instead of RViz ([#4](https://github.com/Quaternar/qaros-qt/pull/4)).

## [0.1.0] - 2026-10-26

The first version. Verified with QAROS 0.9.0-dev.3008+65dc9aeb, Qt 5.15.2 (`win64_msvc2019_64`), MSVC v142 and CMake 3.22 or newer; not tagged.

### Features

- Capture of any `QWidget` into a QarOS GUI panel with `QWidget::render()`, with change detection, a keepalive and at most 15 captures a second ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- Panel sizing for reading in AR (1.2 mm per logical pixel, readability presets) and for the frame QarOS draws around the video (`PanelFrame`) ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- AR input as Qt events: touch with a mouse fallback, per-pointer grabs, double clicks, drags, hover and two hands ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- Popups drawn into their panel's picture, and dialogs as GUI panels of their own with an optional fit to their scroll areas ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- Widget resizes adopted once settled, with a new video sender on the same QarOS panel (`QAR_PANEL_RESIZE` selects a second panel instead) ([#3](https://github.com/Quaternar/qaros-qt/pull/3)).
- Stable common names (`name_scope`), hiding and showing without closing, and the close modes `destroy`, `notice` and `park` ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- Frame brightness and scroll area census for the log, and hidden off-screen panels with a popup guard ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- Video in panels: raw and compressed camera image decoders, a one-slot frame mailbox, a video panel widget and region sources ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
- The example `qar_qt_panel_example` ([#1](https://github.com/Quaternar/qaros-qt/pull/1)).
