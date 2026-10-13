# Configuration

This page lists what you can set: the options of the build, the environment variables that the code and the scripts of this repository read, and the option structs that a host fills in code, each with its default. The code of the library reads no environment variable of its own, so the structs are where its behaviour is chosen; the code of `qar_ogre_adapter`, which runs in every program that links the library, reads a few, and [Environment variables](#environment-variables) names the ones that matter here. [Installation](INSTALL.md) explains how the variables of the layout get their values.

## CMake options and cache variables

The table is generated from the CMake files of the repository. For a variable that names a folder it says which environment variable stands in for it.

| Name | Type | Default | Meaning | Defined in |
|---|---|---|---|---|
| `QAR_BUILD_DOC_SNIPPETS` | BOOL | `ON` | Build the compile-checked documentation snippets | `CMakeLists.txt` |
| `QAR_BUILD_EXAMPLES` | BOOL | `ON` | Build the examples under examples/ | `CMakeLists.txt` |
| `QAR_BUILD_TESTS` | BOOL | `ON` | Build the unit tests and the layering guard tests | `CMakeLists.txt` |
| `QAR_INSTALL` | BOOL | `ON` | Generate the install rules and the CMake package | `CMakeLists.txt` |
| `QAR_PREFIX` | STRING | `$ENV{QAR_PREFIX}` | Install prefix of the upstream repositories | `cmake/QarBuildSupport.cmake` |
| `QAR_QT_ROOT` | PATH | unset: the value of the environment variable `QAR_QT_ROOT` is used; the configure step stops when neither is set in the modules that require it | Qt 5.15.2 (msvc2019_64). The folder must contain `lib/cmake/Qt5/Qt5Config.cmake`. | `cmake/QarBuildSupport.cmake` |
| `QAR_ROS2_INSTALL_ROOT` | PATH | unset: the value of the environment variable `ROS2_INSTALL_ROOT` is used; the configure step stops when neither is set | ROS 2 Humble distribution (ros2-windows). The folder must contain `local_setup.bat`. | `cmake/QarBuildSupport.cmake` |
| `QAR_VENDOR_INCLUDE_DIR` | PATH | `${dir}/third_party/qar/include` | Folder with the vendored qar_streaming.h | `cmake/QarBuildSupport.cmake` |

The build type is `RelWithDebInfo`, as [Build conventions](DEVELOPING.md#build-conventions) explains.

`QAR_VENDOR_INCLUDE_DIR` is set only when the build takes `qar_ogre_adapter` from a checkout with `-DQAR_OGRE_ADAPTER_SOURCE_DIR`, and only when it is empty. Its default is then the folder of the vendored header `qar_streaming.h` in that checkout; the text before the folder names in the table stands for the checkout.

## Consumer-side variables

A program that uses the installed package sets one CMake variable, and a developer can set more:

| Variable | What it does |
|---|---|
| `CMAKE_PREFIX_PATH` | The folders that `find_package` searches. Add the install prefix of the packages, the value of `QAR_PREFIX`; [Linking the package](API.md#linking-the-package) shows the call. |
| `-DQAR_QT_STREAMING_SOURCE_DIR=<checkout>` | A developer override for a project that includes `cmake/QarBuildSupport.cmake` of this repository and finds the library with its command `qar_find_upstream`; a plain `find_package` ignores it. Your build then takes this library from a checkout of this repository instead of an installed package. It installs nothing in that mode, and the checkout must have the version that you request. |
| `-DQAR_OGRE_ADAPTER_SOURCE_DIR=<checkout>` | The same for the build of this repository itself: it takes the modules of qar_ogre_adapter that the build needs from a checkout and not from an installed package. The library alone needs the module `qar_ar_core`. With the examples on, the default, the example also links the components `render` and `panels`, so the build takes the modules `qar_ogre_streaming` and `qar_qar_adapter` from the checkout as well. |

The installed headers are UTF-8 text without a byte order mark, and some comments contain characters outside ASCII; compile your own code with `/utf-8`, as this repository does.

## Environment variables

The table lists every variable that code or a script of this repository reads or sets. The code of the library itself reads none: `QAR_PANEL_CLOSE_MODE` is a name that a host may use for its own setting, as [Close modes](API.md#close-modes) shows, and the other variables belong to the build, to the scripts of the setup, to the unit tests and to the programs that you run. The table does not list the variables that the code of `qar_ogre_adapter` reads, because no file of this repository reads them; the paragraphs below the table name the ones that matter here.

| Variable | Read by | Default | Values and parsing | Effect |
|---|---|---|---|---|
| `QAR_PANEL_CLOSE_MODE` | `examples/qt_panel_example/main.cpp` | `destroy`. | Trimmed and lower-cased: `destroy` or `notice`; empty means unset; any other value runs the default and logs a warning. | What a closing streamed Qt window does to its AR panel. `destroy` closes the panel and destroys its video sender; `notice` leaves the panel up as a closed-window notice and keeps the video sender until the session ends. |
| `TEMP` | `scripts/fix_stale_pixi_paths.ps1`, `scripts/qar_locations.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | None. | The adapter reads `TEMP`, then `TMP`, and uses `<folder>\quaternar` as the QarOS log folder; with neither set it uses the relative folder `quaternar`. Scripts use `$env:TEMP` for scratch files. | Default QarOS log folder and trace folder when the host passes no log folder; scratch folder of several scripts. |
| `AMENT_PREFIX_PATH` | `cmake/QarBuildSupport.cmake`, `scripts/env_setup.ps1` | None. | A list separated by `;` (`:` on other systems). A program that loads ROS message type support reads it through `_dupenv_s` (MSVC) or `getenv` and searches it after the prefixes that the host adds explicitly. | The ament index of the ROS 2 distribution, through which the type-support libraries of ROS message packages (`<pkg>__rosidl_typesupport_*`) are found. The shared CMake support module prepends the distribution root to it for the configure process, and `scripts/env_setup.ps1` prints its value after it has imported the environment of the distribution. |
| `SYSTEMROOT` | `scripts/fix_old_vc_runtime.ps1` | None. | The repair of the C++ runtime joins it with `System32` (with `Sysnative` in a 32-bit PowerShell on 64-bit Windows) to find the system folder whose runtime files it copies; Windows sets the variable for every process. | Windows folder. Only the repair of the C++ runtime of the ROS 2 distribution reads it, to find the newer runtime files of the system folder; no product module reads it. |
| `ROS2_INSTALL_ROOT` | `cmake/QarBuildSupport.cmake`, `scripts/env_setup.ps1`, `scripts/fix_missing_eigen.ps1`, `scripts/fix_missing_openssl_dll.ps1`, and 7 more files, qar_ogre_adapter (its preset and package configuration) | None: a script or the configure step that needs it stops with an error that names the parameter or cache variable and this variable. | A folder. The shared CMake support module uses it when the cache variable `QAR_ROS2_INSTALL_ROOT` is unset or empty; the folder must exist and contain `local_setup.bat`. Scripts use it when the `-Ros2Root` parameter is omitted. The Ogre sample of qar_ogre_adapter takes a non-empty value as the Ogre plugin folder `<root>/opt/rviz_ogre_vendor/bin`. | Root of the ROS 2 Humble Windows distribution: Ogre through `rviz_ogre_vendor`, the ROS command-line tools and the Ogre DLL folder. |
| `QAR_QT_ROOT` | `cmake/QarBuildSupport.cmake`, `scripts/env_setup.ps1`, `CMakePresets.json`, `cmake/qar_qt_streamingConfig.cmake.in` | None: a script or the configure step that needs it stops with an error that names the parameter or cache variable and this variable. | A folder. The shared CMake support module uses it when the cache variable `QAR_QT_ROOT` is unset or empty; the folder must contain `lib/cmake/Qt5/Qt5Config.cmake`. Scripts use it when the `-QtRoot` parameter is omitted. | Root of the Qt 5.15.2 `msvc2019_64` installation. |
| `QAR_DEPS_ROOT` | `scripts/fetch_dependencies.ps1`, `scripts/fix_missing_eigen.ps1`, `scripts/fix_missing_openssl_dll.ps1`, `scripts/fix_missing_spdlog_dll.ps1`, and 2 more files | None: a script that needs it stops with an error that names the parameter and this variable. | A folder. Scripts use it when the `-DepsRoot` parameter is omitted. | Dependency root of the setup kit: the downloads folder and the build trees of the distribution repairs live below it. |
| `QAR_PREFIX` | `cmake/QarBuildSupport.cmake`, `CMakePresets.json` | None: the configure step continues without it, and a script that needs it stops with an error that names the parameter and this variable. | One folder, or several separated by semicolons (CMake reads the value as a list); every folder must exist. CMake prepends them to `CMAKE_PREFIX_PATH`. Scripts use it when the `-Prefix` parameter is omitted. | Install prefix of the repositories: the libraries are installed into it, and a repository that depends on them finds them through it. |
| `QAR_SDK_ROOT` | `scripts/fix_old_vc_runtime.ps1`, qar_ogre_adapter (`qar_qar_adapter`, its preset) | None. | A folder. CMake (the adapter module) uses it when the cache variable `QAR_SDK_ROOT` is unset or empty; an unset or wrong value only gives a configure warning, because nothing links against the SDK. Scripts use it when the `-SdkRoot` parameter is omitted. | Root of the QarOS SDK install tree (`bin`, `include`). It is used by the configure-time check and by the scripts that need the SDK; at run time the DLL is found through the runtime binaries folder that the host passes (`runtime_binaries_folder_path`). |
| `QAR_VENV` | `scripts/env_setup.ps1`, `scripts/fix_missing_openssl_dll.ps1`, `scripts/fix_stale_pixi_paths.ps1` | None: a script that needs it stops with an error that names the parameter and this variable; `fix_missing_openssl_dll.ps1` tries `tar` instead; `fix_stale_pixi_paths.ps1` prints a warning and goes on without the numpy headers. | A folder. Scripts use it when the `-Venv` parameter is omitted; `fix_missing_openssl_dll.ps1` takes `Scripts\python.exe` below it as the Python that unpacks the OpenSSL package when `-Python` is omitted; `fix_stale_pixi_paths.ps1` takes the numpy headers from the numpy package below it when `-NumpySourceInclude` is omitted. | Python 3.10 virtual environment (the folder with `Scripts\python.exe`). |
| `PYTHONPATH` | `cmake/QarBuildSupport.cmake` | None. | The shared CMake support module puts `<ROS2_INSTALL_ROOT>/Lib/site-packages` in front of the existing value for the configure process when that folder exists. | Lets the ament CMake modules import `ament_package` from the Python of the ROS 2 distribution while a project is configured. |
| `QT_QPA_PLATFORM` | `qar_qt_streaming/test/test_main.cpp` | `offscreen` (tests only). | The Qt test runner sets it to `offscreen` with `qputenv` when `qEnvironmentVariableIsEmpty` reports it empty. | Qt platform plugin for the GUI tests. |
| `QT_QPA_PLATFORM_PLUGIN_PATH` | `scripts/env_setup.ps1` | None. | Read by Qt, not by repository code; set by `scripts/env_setup.ps1`. | Directory of the Qt platform plugins, the folder `platforms`, for the programs that the shell starts. Set by `scripts/env_setup.ps1`. |
| `QT_PLUGIN_PATH` | - | None. | Read by Qt, not by repository code; set by `scripts/env_setup.ps1`. | Directory of the Qt plugins. Set by `scripts/env_setup.ps1`. |
| `PATH` | `scripts/env_setup.ps1`, `scripts/qar_locations.ps1`, qar_ogre_adapter | None. | Read by the Windows loader. | The folders in which Windows looks for the DLLs of a program that it starts: the Qt, Ogre and ROS DLL folders must be on it for a program that imports them. The scripts prepend them. Set by `scripts/env_setup.ps1`, `scripts/qar_locations.ps1`. |

The example of this repository reads `QAR_PANEL_CLOSE_MODE` and passes the text to `parse_panel_close_mode`: the accepted words are `destroy` and `notice`, in any case and with blanks around them, an unset or empty variable gives `destroy`, and any other value gives `destroy` and one warning.

Two variables belong to the session factory of the component `render` of `qar_ogre_adapter`, which the example uses. The factory sets `QAR_LOG_SEVERITY` in the process to the severity of the QarOS library log, and it starts the zenoh logger when `QAR_ZENOH_LOG` is set and that severity is `trace` or `debug`; with `QAR_ZENOH_LOG` unset, the example prints the line `zenoh logging: off (QAR_ZENOH_LOG not set)`. The CONFIGURATION document of `qar_ogre_adapter` describes both.

Three more variables are worth knowing when you run a program that links the library. `PATH` has to name the `bin` folder of Qt, because Windows finds the DLLs of a program through it. The Ogre `bin` folder of the ROS 2 distribution belongs on `PATH` only for a program that runs the Ogre-based rendering of `qar_ogre_adapter`, because only then does the import table of the program list the Ogre library; the examples of `qar_ogre_adapter` are such programs. The example of this repository is not, although its build needs the distribution and it opens its QarOS session with the component `render`: its import table lists the three Qt libraries and the Microsoft runtime, and it ran with the Qt `bin` folder alone added to `PATH`. `QT_QPA_PLATFORM_PLUGIN_PATH` names the folder `platforms` with `qwindows.dll`, which a program that shows a window needs. And `QAR_VIDEO_TIMING_DIR`, which the timing log of the core module `qar_ar_core` reads when a panel stream asks for it, switches on the per-frame timing of video panels: every panel whose content is a video source writes one CSV file, `video_<key>.csv`, into that folder. The unit tests set `QT_QPA_PLATFORM` to `offscreen` when it is empty, so that they open no window.

## Runtime option structs

These are the structs that a host fills in code. Every default below is the value that the struct has when you change nothing. [API](API.md) shows how a host fills them.

### The configuration of the manager

`QtPanelStreamingConfig` in `qt_panel_stream_manager.hpp`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `panel_sender_factory` | `std::shared_ptr<IQarPanelSenderFactory>` | none | The only seam to QarOS; required. The constructor of the manager throws `std::invalid_argument` without it. |
| `diagnostics` | `PanelDiagnosticsSink *` | null | Receives the lines of the manager. Without a sink the manager only counts. |
| `clock` | `std::shared_ptr<IClock>` | null | The time source; null makes a `SteadyClock`. A test passes a clock that it advances by hand. |
| `input_sink` | `IQarPanelInputSink *` | null | Receives a coarse hover or click for every resolved interaction. The interactions reach the widgets without it. |
| `input_source` | `std::shared_ptr<IQarPanelInputSource>` | null | The AR pointer samples and the panels that the operator closed in AR, drained on every tick. Without it the host feeds `handle_sample` itself. |
| `pump_input_in_tick` | `bool` | `true` | Whether `tick()` drains the input source itself. Set it to false in a host whose tick runs from a `QTimer`, and call `pump_input()` from a posted event. |
| `dialog_panels` | `bool` | `true` | Gives a dialog that a streamed panel opened a panel of its own. False leaves the dialog invisible in AR and answerable on the desktop only. |
| `dialog_panel_offset_m` | `double` | `0.5` | The step in X, in metres, between a panel and each dialog panel that it owns. |
| `unserved_warn_seconds` | `double` | `5.0` | How long a panel may hold a sender without a single frame before the manager says so; 0 switches the report off. |
| `close_mode` | `PanelCloseMode` | `Destroy` | What a closed window does to its AR panel: `Destroy` or `Notice`. |
| `max_tombstoned_panels` | `std::uint32_t` | `16` | How many panels that show a notice the manager may hold at once; meaningful only for `Notice`; 0 means no limit. |
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

### Off-screen panels and the close mode

The size of an off-screen panel and the close mode have no struct of their own: the host parses a text that it gets from a setting of its own, and the parsers never read the environment.

| Parser | Accepts | Default | Warning |
|---|---|---|---|
| `parse_offscreen_size` | `WIDTHxHEIGHT` in decimal, `x` or `X`, optional spaces around the value and the `x` | 480 by 720 pixels | a text that is not a size, and a side outside 200 to 2000, which is clamped |
| `parse_panel_close_mode` | `destroy` or `notice`, in any case, with blanks around the word | `Destroy` | any other text, naming the text and the two words |

An unset, empty or blank text is not a mistake for either parser: it gives the default without a warning, whereas a text that was given and cannot be used gives the default and one warning to print, because a silent fallback would let a typo such as `notise` run in the wrong mode.

### The smaller constants

| Name | Value | Where it acts |
|---|---|---|
| `PanelOverlayAttribution::window_ms()` | `1500` ms | how long after an activating AR action a new dialog still counts as caused by that panel |
| `PanelOverlayOptions` | all five fields `true` | which kinds of owned top-level window a scan returns, whether a dialog may be attributed by the input, and whether the scan may decide an attribution |
| `TickIntervalEstimator::kSmoothing` | `0.2` | the weight of a new gap in the estimate of the tick interval |
| `TickIntervalEstimator::kMaxSampleSeconds` | `0.1` | one gap is clamped to this, so that a stall of the GUI is a blip and not a new granularity |
| `PanelTombstoneStyle::body_font_px` | `12.0` | the body size of the notice, in logical pixels |
| `PanelTombstoneStyle::minimum_font_px` | `8.7` | the size below which the fit of the notice never shrinks the text |
| `kTombstoneWidthMargin` | `0.86` | the part of the width that the longest line of the notice may occupy |
| `kTombstoneHeadlineScale` | `1.45` | the size of the headline relative to the body |
| `kTombstoneAvgAdvanceRatio` | `0.55` | the mean glyph advance as a part of the font size, which the fit uses instead of font metrics |
| `LatestVideoFrame` rate | weight `0.2` | the weight of a new interval in the smoothed rate of `fps()` |
| `VideoPanelWidget` width | `640` | the initial width, in logical pixels, when the host passes none |
