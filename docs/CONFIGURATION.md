# Configuration

This page lists what you can set: the options of the build, the environment variables that the code of this repository reads, and the option structs that a host fills in code, each with its default. The library reads no environment variable of its own, so the structs are where its behaviour is chosen; the example and the timing log read a few, and [Environment variables](#environment-variables) names them. [Installation](INSTALL.md) explains how the variables of the layout get their values.

## CMake options and cache variables

The table is generated from the CMake files of the repository. For a variable that names a folder it says which environment variable stands in for it.

| Name | Type | Default | Meaning | Defined in |
|---|---|---|---|---|
| `QAR_BUILD_DOC_SNIPPETS` | BOOL | `${PROJECT_IS_TOP_LEVEL}` | Build the compile-checked documentation snippets | `CMakeLists.txt` |
| `QAR_BUILD_EXAMPLES` | BOOL | `${PROJECT_IS_TOP_LEVEL}` | Build the examples under examples/ | `CMakeLists.txt` |
| `QAR_BUILD_TESTS` | BOOL | `${PROJECT_IS_TOP_LEVEL}` | Build the unit tests and the layering guard tests | `CMakeLists.txt` |
| `QAR_INSTALL` | BOOL | `${PROJECT_IS_TOP_LEVEL}` | Generate the install rules and the CMake package | `CMakeLists.txt` |
| `QAR_QT_ROOT` | PATH | unset: the value of the environment variable `QAR_QT_ROOT` is used; the configure step stops when neither is set in the modules that require it | ${_qq_WHAT}. The folder must contain `${_qq_markers}`. | `cmake/QarBuildSupport.cmake` |
| `QAR_QT_STREAMING_SDK_DEFINES` | STRING | `QAR_ENABLE_DYNAMIC_LOADING` | Macros of the QarOS C header that the qaros backend is compiled with | `CMakeLists.txt` |
| `QAR_QT_STREAMING_WITH_QAROS` | BOOL | `ON` | Build the qaros backend (needs the QarOS C++ SDK headers) | `CMakeLists.txt` |
| `QAR_QAROS_SOURCE_DIR` | PATH | `external/qaros` of the repository | Source folder of the public QarOS repository (default: the submodule external/qaros) | `CMakeLists.txt` |
| `QAR_STREAMING_CPP_INCLUDE_DIR` | PATH | empty | Folder with qar_streaming_cpp/Config.hpp and qar_streaming.h (default: the submodule external/qaros) | `CMakeLists.txt` |

The build type is `RelWithDebInfo`, as [Build conventions](DEVELOPING.md#build-conventions) explains.

`QAR_QT_MAJOR` is 5 or 6 and chooses the major version of Qt. It is not a cache entry of the table: pass `-DQAR_QT_MAJOR=6` to insist on Qt 6; without it the build takes the version from the kit that `QAR_QT_ROOT` names, and from the Qt packages that CMake finds when the variable is unset. The configure step stops when the kit does not match the value.

The options of the table that choose what is built: `QAR_QT_STREAMING_WITH_QAROS` builds the layer `qaros` (the default is ON; with OFF the project needs no QarOS header), `QAR_BUILD_TESTS`, `QAR_BUILD_EXAMPLES` and `QAR_BUILD_DOC_SNIPPETS` are ON when the project is the top level of the build and OFF when it is added to another project, and `QAR_INSTALL` generates the install rules and the CMake package under the same condition. `QAR_STREAMING_CPP_INCLUDE_DIR` names the folder with the QarOS C++ SDK headers, and `QAR_QT_STREAMING_SDK_DEFINES` lists the macros of the QarOS C header that the layer `qaros` is compiled with; a program that links the layer has to be compiled with the same macros, and the link fails when it is not.

The QarOS headers come from the first of these that applies: the targets `qar-streaming-cpp` and `qar-streaming-c-headers` of a parent project; the folder `QAR_STREAMING_CPP_INCLUDE_DIR`, which must hold both `qar_streaming_cpp/Config.hpp` and `qar_streaming.h`; the targets of the same names of the public QarOS repository in `QAR_QAROS_SOURCE_DIR`, by default the git submodule `external/qaros` (`git submodule update --init`). The C++ headers and the C header therefore always come from one QarOS revision. The target `qar-streaming-c-headers` of the QarOS repository adds `QAR_ENABLE_DYNAMIC_LOADING` to every target that links it, and the build switches off the C examples of that repository (`BUILD_CORE_EXAMPLES`).

## Consumer-side variables

A program that uses the installed package sets one CMake variable, and it may set more:

| Variable | What it does |
|---|---|
| `CMAKE_PREFIX_PATH` | The folders that `find_package` searches. Add the install prefix of the package, the value of `QAR_PREFIX`, and the Qt kit; [Linking the package](API.md#linking-the-package) shows the call. |
| `QAR_STREAMING_CPP_INCLUDE_DIR` | The folder with the QarOS C++ SDK headers for the component `qaros` of the package; without it the package uses the copy of the QarOS headers that it installed, or the folder that it was built against. |

The installed headers are UTF-8 text without a byte order mark, and some comments contain characters outside ASCII; compile your own code with `/utf-8`, as this repository does.

## Environment variables

The table lists every variable that code or a script of this repository reads or sets. The code of the library itself reads none: `QAR_PANEL_CLOSE_MODE` and `QAR_PANEL_RESIZE` are names that a host may use for its own settings, as [Close modes](API.md#close-modes) shows, and the other variables belong to the build, to the unit tests and to the programs that you run.

| Variable | Read by | Default | Values and parsing | Effect |
|---|---|---|---|---|
| `QAR_PANEL_CLOSE_MODE` | `examples/qt_panel_example/main.cpp` | `destroy`. | Trimmed and lower-cased: `destroy`, `notice` or `park`; empty means unset; any other value runs the default and logs a warning. | What a closing streamed Qt window does to its AR panel. `destroy` closes the panel and destroys its video sender; `notice` leaves the panel up as a closed-window notice and keeps the video sender until the session ends; `park` hides the panel in AR and keeps it, its video sender and its subscriptions until the session ends, and a window that is added again shows the same panel. |
| `QAR_PANEL_RESIZE` | `examples/qt_panel_example/main.cpp` | `swap`. | Trimmed and case-insensitive: `swap`, or `new-panel` (also `new_panel`, `newpanel`). An empty value counts as unset; any other value runs the default and logs a warning. | What a size change of a streamed window does to its QarOS panel. `swap` keeps the QarOS panel, releases its video sender and attaches a new sender of the new size to the same panel; a sender that cannot detach makes a new panel instead. `new-panel` makes a second QarOS panel for each size, hides and keeps the old one and shows it again when the size returns. |
| `QAR_VIDEO_TIMING_DIR` | `qar_qt_streaming/seam/include/qar_qt_streaming/seam/timing_log.hpp` | Unset: no timing files. | A non-empty value is a folder, created lazily by the writer thread; an unset or empty value disables the feature. | Opt-in per-frame stage timing: when the variable is set, a background writer thread writes timing CSV files into the folder. |
| `LOCALAPPDATA` | `examples/qt_panel_example/main.cpp` | Set by Windows. | A folder. When it is empty, the program takes `AppData\Local` under `USERPROFILE`. | Base of the folder in which a program that joins the QAROS Hub keeps its identity and its stored pairing, `Quaternar\<product>\<program>` below it; a script reads it to find the endpoint file of the Hub. |
| `QAR_QT_ROOT` | `cmake/QarBuildSupport.cmake`, `CMakePresets.json`, `cmake/qar_qt_streamingConfig.cmake.in` | None: the configure step stops with an error that names this variable when it needs the kit and finds no Qt in another way. | A folder. The shared CMake support module uses it when the cache variable `QAR_QT_ROOT` is unset or empty, puts it in front of `CMAKE_PREFIX_PATH` and checks that it holds `lib/cmake/Qt5/Qt5Config.cmake` or `lib/cmake/Qt6/Qt6Config.cmake`. Scripts use it when the `-QtRoot` parameter is omitted. | Root of the Qt kit that the build uses: the Qt 5.15.2 `msvc2019_64` kit of the documentation, or a Qt 6 kit. |
| `QAR_SDK_ROOT` | documentation and scripts only | None. | A folder. The build does not read it: the QarOS headers come from the submodule `external/qaros` or from `QAR_STREAMING_CPP_INCLUDE_DIR`. Scripts use it when the `-SdkRoot` parameter is omitted. | Root of the installed QAROS, the folder that holds `bin` and `include` and that the installer records as `InstallFolder` in the registry. It is used by the scripts and checks that need the install tree; at run time the library is found by the vendor lookup (the registry, `QAROS_RUNTIME_DIR`, `PATH`) or by the library path of the owner configuration. |
| `QT_QPA_PLATFORM` | `qar_qt_streaming/test/test_main.cpp` | `offscreen` (tests only). | The Qt test runner sets it to `offscreen` with `qputenv` when `qEnvironmentVariableIsEmpty` reports it empty. | Qt platform plugin for the GUI tests. |

The example of this repository reads `QAR_PANEL_CLOSE_MODE` and passes the text to `parse_panel_close_mode`: the accepted words are `destroy`, `notice` and `park`, in any case and with blanks around them. An unset or empty variable is not a choice, and the example then uses `park`; any other value gives `destroy` from the parser and one warning.

Three more variables are worth knowing when you run a program that links the library. `PATH` has to name the `bin` folder of Qt, because Windows finds the DLLs of a program through it; the import table of the example lists the Qt libraries and the Microsoft runtime, and it ran with the Qt `bin` folder alone added to `PATH`. `QT_QPA_PLATFORM_PLUGIN_PATH` names the folder `platforms` with `qwindows.dll`, which a program that shows a window needs. And `QAR_VIDEO_TIMING_DIR`, which the timing log of the layer `seam` reads when a panel stream asks for it, switches on the per-frame timing of video panels: every panel whose content is a video source writes one CSV file, `video_<key>.csv`, into that folder. The unit tests set `QT_QPA_PLATFORM` to `offscreen` when it is empty, so that they open no window.

## Runtime option structs

These are the structs that a host fills in code. Every default below is the value that the struct has when you change nothing. [API](API.md) shows how a host fills them.

### The configuration of the manager

`QtPanelStreamingConfig` in `qt_panel_stream_manager.hpp`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `panel_sender_factory` | `std::shared_ptr<seam::IPanelSenderFactory>` | none | The only seam to QarOS; required. The constructor of the manager throws `std::invalid_argument` without it. |
| `diagnostics` | `PanelDiagnosticsSink *` | null | Receives the lines of the manager. Without a sink the manager only counts. |
| `clock` | `std::shared_ptr<IClock>` | null | The time source; null makes a `SteadyClock`. A test passes a clock that it advances by hand. |
| `input_sink` | `IQarPanelInputSink *` | null | Receives a coarse hover or click for every resolved interaction. The interactions reach the widgets without it. |
| `input_source` | `std::shared_ptr<seam::IPanelInputSource>` | null | The AR pointer samples and the panels that the operator closed in AR, drained on every tick. Without it the host feeds `handle_sample` itself. |
| `pump_input_in_tick` | `bool` | `true` | Whether `tick()` drains the input source itself. Set it to false in a host whose tick runs from a `QTimer`, and call `pump_input()` from a posted event. |
| `dialog_panels` | `bool` | `true` | Gives a dialog that a streamed panel opened a panel of its own. False leaves the dialog invisible in AR and answerable on the desktop only. |
| `dialog_panel_offset_m` | `double` | `0.5` | The step in X, in metres, between a panel and each dialog panel that it owns. |
| `dialog_height_factor` | `double` | `1.0` | How much taller than it is a dialog is made on the desktop before its panel is made, once for each dialog window; only a factor above 1.0 has an effect, and a dialog size adjuster takes its place. |
| `ar_frame` | `PanelFrame` | the frame of the Visualizer, [below](#the-frame-of-a-panel) | The frame that QarOS draws around the video of the AR panels of this manager; it replaces the `ar_frame` of the options of every panel that the manager makes. |
| `unserved_warn_seconds` | `double` | `5.0` | How long a panel may hold a sender without a single frame before the manager says so; 0 switches the report off. |
| `close_mode` | `PanelCloseMode` | `Destroy` | What a closed window does to its AR panel: `Destroy`, `Notice` or `Park`. At the end of the session `Notice` and `Park` hide and detach the panels instead of closing them. |
| `name_scope` | `qar_ar_core::NameScope` | empty members | The program, the instance and the host that every common name of a QarOS panel is made from, so that a panel that was hidden when the last session ended is found and shown again by the next start. |
| `resize_mode` | `PanelResizeMode` | `Swap` | What a size change of a window does to its QarOS panel: `Swap` keeps the panel and attaches a new video sender to it, `NewPanel` makes a second panel and keeps the old one hidden. The host reads `QAR_PANEL_RESIZE` with `parse_panel_resize_mode`. |
| `max_tombstoned_panels` | `std::uint32_t` | `16` | How many panels that show a notice or are parked the manager may hold at once; meaningful only for `Notice` and `Park`; 0 means no limit. |
| `max_replaced_panels` | `std::uint32_t` | `32` | How many QarOS panels that size changes replaced the manager may keep hidden at once in the resize mode `NewPanel`, apart from the cap above; 0 means no limit. |
| `pointer_state` | `PointerStateConfig` | 0.4 s and 5 px | The double click interval and distance. |

### The options of a panel

`PanelStreamOptions` in `qt_panel_stream.hpp`, one for each widget that you add.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `display_name` | `std::string` | empty | The title that QarOS shows. Empty makes the manager use the key. |
| `placement` | `PanelPlacement` | the origin, no rotation | Where the panel hangs; the size in it is overwritten at every tick from the widget, so only the pose is yours. |
| `meters_per_logical_px` | `double` | `0.0012` | The physical size of one logical pixel in the room, in metres. |
| `render_scale` | `double` | `1.0` | The supersample on top of the pixel ratio of the display; 1.0 captures at the ratio of the display. |
| `buffer_alignment` | `std::uint32_t` | `2` | The buffer sizes are rounded up to a multiple of this. |
| `max_buffer_dimension` | `std::uint32_t` | `4096` | The limit of each side of the buffer; a larger widget streams at a lower pixel ratio. |
| `max_fps` | `std::uint32_t` | `15` | The most captures a second; 0 captures on every tick. |
| `capture_on_frame_arrival` | `bool` | `true` | A panel with a video source is captured when a frame arrives, with `max_fps` as the upper bound. False restores the strict limiter for it. |
| `keepalive_seconds` | `double` | `1.0` | An unchanged picture is sent again after this long; 0 switches the resend off. |
| `input` | `PanelInputOptions` | see below | How the AR pointer reaches the widget. |
| `resize_threshold_px` | `std::uint32_t` | `8` | A change of the buffer smaller than this in both dimensions is ignored. |
| `resize_settle_seconds` | `double` | `0.5` | A larger change has to hold for this long ... |
| `resize_settle_ticks` | `std::uint32_t` | `3` | ... and across this many consecutive ticks before it is adopted. Zero for all three adopts every change. |
| `ar_frame` | `PanelFrame` | the frame of the Visualizer, [below](#the-frame-of-a-panel) | The frame that QarOS draws around the video: the AR panel is made as tall as its video needs inside the frame, plus the fit margin, and keeps its width. The manager replaces it for every panel that it makes. |
| `dialog_panel` | `bool` | `false` | The panel is the panel of a dialog window: it keeps the height of its video and fits its width to the frame. The manager sets it for the panels of the dialogs. |

### The options of the input

`PanelInputOptions` in `qt_panel_input_injector.hpp`, in `PanelStreamOptions::input`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `enabled` | `bool` | `true` | False makes a panel that streams out and takes no input. |
| `delivery_mode` | `InputDeliveryMode` | `TouchWithMouseFallback` | `MouseOnly`, `TouchOnly` or `TouchWithMouseFallback`. |
| `deliver_hover` | `bool` | `true` | Enter, leave and hover events and button-less moves. |
| `apply_focus_policy` | `bool` | `true` | A press gives the focus to the first widget of the chain that takes click focus. |
| `route_to_popups` | `bool` | `true` | An open popup takes every pointer, and a press outside it closes it. |
| `block_behind_modal` | `bool` | `true` | The pointers on a panel reach nothing while a modal dialog of the panel is up. |

### The geometry of a panel

`PanelGeometryInput` in `panel_geometry.hpp`, which `compute_panel_geometry` reads. The stream fills it from the widget and from the options of the panel; the defaults matter to a host that calls the function itself. The readability profile has its own table in [DPI, buffers and AR readability](ARCHITECTURE.md#dpi-buffers-and-ar-readability), where its formulas are.

| Field | Default | Meaning |
|---|---|---|
| `logical_width`, `logical_height` | `0` | The logical size of the widget; a zero is refused. |
| `device_pixel_ratio` | `1.0` | `QWidget::devicePixelRatioF()`. |
| `meters_per_logical_px` | `0.0012` | The physical size of a logical pixel. |
| `render_scale` | `1.0` | The supersample on top of the pixel ratio. |
| `buffer_alignment` | `2` | The alignment of the buffer sides. |
| `max_buffer_dimension` | `4096` | The limit of a buffer side; 0 means the default. |
| `frame` | the frame of the Visualizer, [below](#the-frame-of-a-panel) | The frame that QarOS draws around the video: its vertical part and the fit margin are added to the height of the panel, and its horizontal part is taken from the width of the video. |
| `narrow_for_frame` | `false` | Fit the width to the frame instead of the height, as the panel of a dialog does. |

### The frame of a panel

`PanelFrame` in `panel_geometry.hpp`: the four insets of the frame that QarOS draws around the video of a stream panel, and the fit margin, in metres. [DPI, buffers and AR readability](ARCHITECTURE.md#dpi-buffers-and-ar-readability) explains what each one does. All five are finite and not negative, and all zeros is no frame.

| Field | Default | Meaning |
|---|---|---|
| `top_m` | `0.066` | The title bar above the video. |
| `bottom_m` | `0.003` | Below the area that the video is centred in. |
| `left_m` | `0.003` | Between the left edge of the panel and the video. |
| `right_m` | `0.003` | Between the video and the right edge of the panel. |
| `fit_margin_m` | `0.003` | How much taller than the video the area inside the insets is made. |

### Off-screen panels and the close mode

The size of an off-screen panel and the close mode have no struct of their own: the host parses a text that it gets from a setting of its own, and the parsers never read the environment.

| Parser | Accepts | Default | Warning |
|---|---|---|---|
| `parse_offscreen_size` | `WIDTHxHEIGHT` in decimal, `x` or `X`, optional spaces around the value and the `x` | 480 by 720 pixels | a text that is not a size, and a side outside 200 to 2000, which is clamped |
| `parse_panel_close_mode` | `destroy`, `notice` or `park`, in any case, with blanks around the word | `Destroy` | any other text, naming the text and the three words |

An unset, empty or blank text is not a mistake for either parser: it gives the default without a warning, whereas a text that was given and cannot be used gives the default and one warning to print, because a silent fallback would let a typo such as `notise` run in the wrong mode.

### The smaller constants

| Name | Value | Where it acts |
|---|---|---|
| `PanelOverlayAttribution::window_ms()` | `1500` ms | how long after an activating AR action a new dialog still counts as caused by that panel |
| `PanelOverlayOptions` | all five fields `true` | which kinds of owned top-level window a scan returns, whether a dialog may be attributed by the input, and whether the scan may decide an attribution |
| `kMinNarrowedWidthShare` | `0.5` | the least share of its nominal width that the panel of a dialog keeps |
| `QtPanelStreamManager::kDialogFitIntervalSeconds`, `kDialogFitMaxRounds` | `0.1` s, `24` | the least time between two calls of the dialog size adjuster for one dialog, and the number of calls after which the dialog is streamed whatever the adjuster says |
| `QtPanelStream::kKeptHideRetries`, `kKeptHideRetrySeconds` | `3`, `1.0` s | how many more times, and how far apart, a hide that QarOS refused is asked for when a size change kept the old panel |
| `QtPanelStream::kBurstStreakTicks`, `kBurstFrames` | `10`, `3` | the streak of not-ready ticks that earns a burst of unchanged frames, and the length of the burst |
| `QtPanelStream::kCensusFollowUpSeconds` | `1.5` s | when the second census of the scroll areas of a pair is taken |
| `kBrightnessTopFraction`, `kLuminanceSampleStep` | `0.15`, `8` | the share of the rows that count as the top rows of a frame, and the stride of the sample that measures its brightness |
| `TickIntervalEstimator::kSmoothing` | `0.2` | the weight of a new gap in the estimate of the tick interval |
| `TickIntervalEstimator::kMaxSampleSeconds` | `0.1` | one gap is clamped to this, so that a stall of the GUI is a blip and not a new granularity |
| `PanelTombstoneStyle::body_font_px` | `12.0` | the body size of the notice, in logical pixels |
| `PanelTombstoneStyle::minimum_font_px` | `8.7` | the size below which the fit of the notice never shrinks the text |
| `kTombstoneWidthMargin` | `0.86` | the part of the width that the longest line of the notice may occupy |
| `kTombstoneHeadlineScale` | `1.45` | the size of the headline relative to the body |
| `kTombstoneAvgAdvanceRatio` | `0.55` | the mean glyph advance as a part of the font size, which the fit uses instead of font metrics |
| `LatestVideoFrame` rate | weight `0.2` | the weight of a new interval in the smoothed rate of `fps()` |
| `VideoPanelWidget` width | `640` | the initial width, in logical pixels, when the host passes none |
