# API reference

This page lists the public headers of the library, the types and functions that a host uses, and the rules that come with them. [Architecture](ARCHITECTURE.md) explains how the parts fit together, and [Configuration](CONFIGURATION.md) lists the default values of the option structs. The code on this page is compiled with the repository; its parameters stand for the objects that your program owns.

## Linking the package

A consumer finds the package with `find_package` and links one imported target. The package version is compatible with the versions of the same minor number while the major number is 0, so a request for 0.1.0 is met by 0.1.x:


```cmake
find_package(qar_qt_streaming 0.1.0 CONFIG REQUIRED)
```

The target that compiles the code of this page links it in the same way:

```cmake
target_link_libraries(qar_qt_streaming_doc_snippets PRIVATE
  qar_qt_streaming::qar_qt_streaming
)
```

| Target | What it brings with it |
|---|---|
| `qar_qt_streaming::qar_qt_streaming` | C++17, the include folder of the library, `qar_ogre_adapter::qar_ar_core`, and the Qt targets `Qt5::Core`, `Qt5::Gui` and `Qt5::Widgets` |

The library calls no QarOS function. A program that streams to QarOS also needs a session and a panel factory, which are the components `render` and `panels` of qar_ogre_adapter, and the example links exactly these:

```cmake
target_link_libraries(qar_qt_panel_example PRIVATE
  qar_qt_streaming::qar_qt_streaming
  qar_ogre_adapter::render
  qar_ogre_adapter::panels
  Qt5::Widgets
)
```

Include the headers of the library before the headers of Qt. `panel_pointer_state.hpp` declares a method named `emit`, which only parses while the `emit` macro of Qt does not exist yet, and a header of Qt defines it; defining `QT_NO_KEYWORDS` lifts the rule. The code is in the namespace `qar_qt_streaming`, and its results are the `qar_ar_core::Result` and `qar_ar_core::Status` of the core module, whose values are `Ok`, `InvalidArgument`, `InvalidState`, `AlreadyExists`, `NotFound`, `GuiThreadRequired` and `SenderFailed`.

## Header map

Every public header of the library. The sections below describe the types.

| Header | What it declares |
|---|---|
| `qt_panel_stream_manager.hpp` | `QtPanelStreamManager`, `QtPanelStreamingConfig`, `IQarPanelInputSink`, `PanelPointerEvent`, `DialogFitStep`, `DialogSizeAdjuster`, `DialogPoseAdjuster`, `make_census_dialog_adjuster` |
| `qt_panel_stream.hpp` | `QtPanelStream`, `PanelStreamOptions`, `apply_readability`, `ar_panel_height_m`, `ReplacedPanelFate`, `SizeRebuildReport` |
| `qt_widget_capture.hpp` | `QtWidgetCapture`, `hash_pixels`, `PanelOverlayAttribution`, `PanelOverlayOptions`, the scans `collect_panel_popups`, `collect_panel_overlays` and `panel_owned_modal_window`, and the ownership tests |
| `capture_gate.hpp` | `decide_capture`, `CaptureGateInput`, `CaptureGateVerdict`, `TickIntervalEstimator` |
| `qt_panel_input_injector.hpp` | `QtPanelInputInjector`, `PanelInputOptions`, `InputDeliveryMode`, `InputInjectionStats` |
| `panel_pointer_state.hpp` | `PanelPointerStateMachine`, `PointerAction`, `PointerActionKind`, `ResolvedPointerSample`, `PointerTarget`, `PointerStateConfig`, `PointerStateDiagnostics` |
| `panel_hit_test.hpp` | `PanelHit`, `hit_test_panel_point`, `intersect_panel_ray`, the conversions between a hit and a widget position, `PanelHitCandidate`, `PanelPick`, `pick_panel_for_point`, `pick_panel_for_ray` |
| `panel_geometry.hpp` | `PanelGeometryInput`, `PanelGeometry`, `PanelFrame`, `FrameNarrowing`, `compute_panel_geometry`, `align_up`, `kDefaultMetersPerLogicalPx`, `kDefaultArFrameTopM`, `kDefaultArFrameBottomM`, `kDefaultArFrameLeftM`, `kDefaultArFrameRightM`, `kDefaultArFitMarginM`, `kMinNarrowedWidthShare` |
| `ar_readability.hpp` | `ReadabilityProfile`, `ReadabilityPreset`, `ReadabilityRecommendation`, `readability_preset`, `recommend_readability`, `subtended_arcmin`, `ceil_to_half` |
| `offscreen_panel.hpp` | `show_offscreen`, `is_offscreen_panel`, `parse_offscreen_size`, `OffscreenSizeChoice`, `clamp_offscreen_side` |
| `offscreen_popup_guard.hpp` | `install_offscreen_popup_guard`, `offscreen_popup_guard_installed`, `offscreen_popup_guard_stats`, `popup_owner_widget`, `is_owned_by_offscreen_panel`, `is_offscreen_popup`, `close_offscreen_popups`, `remove_offscreen_popup_guard` |
| `panel_close_mode.hpp` | `PanelCloseMode`, `PanelCloseModeChoice`, `parse_panel_close_mode`, `kPanelCloseModeEnv` |
| `panel_tombstone.hpp` | `PanelTombstoneText`, `PanelTombstoneStyle`, `make_panel_tombstone_widget`, `tombstone_body_font_px`, `tombstone_is_clipped` |
| `scroll_census.hpp` | `ScrollCensus`, `ScrollAreaCensus`, `take_scroll_census`, `describe_scroll_census`, `kScrollsByDesignProperty` |
| `frame_luminance.hpp` | `FrameBrightness`, `mean_luminance`, `frame_brightness`, `kBrightnessNotMeasured`, `kBrightnessTopFraction`, `kLuminanceSampleStep` |
| `video_frame.hpp` | `RawImageView`, `decode_raw_image`, `decode_compressed_image`, `aspect_fit_rect` |
| `latest_video_frame.hpp` | `LatestVideoFrame`, `VideoFrameTrace` |
| `video_panel_widget.hpp` | `VideoPanelWidget` |
| `clock.hpp` | `IClock`, `SteadyClock` |

The header `mock_panel_sender.hpp` belongs to the header-only target `qar_qt_streaming_test_support` and is not part of the library; [Testing](TESTING.md#test-doubles) describes the recording doubles in it.

## QtPanelStreamManager

`QtPanelStreamManager` is the entry point: it owns one stream for each widget and runs them all from one `tick()`. Its constructor throws `std::invalid_argument` when the `panel_sender_factory` of the `QtPanelStreamingConfig` is null; the input source, the sink, the diagnostics sink and the clock are optional. In the code, `factory` is the panel sender factory of the host, `input` its input source, `sink` a diagnostics sink and `input_sink` an input sink. [Configuration](CONFIGURATION.md#runtime-option-structs) lists every field with its default.

```cpp
qqs::QtPanelStreamingConfig config;
config.panel_sender_factory = factory;      // required: the one seam to QarOS
config.input_source = input;                // optional: the AR pointer samples; drained on every tick
config.diagnostics = sink;                  // optional: the lines of the manager; without it they are only counted
config.input_sink = input_sink;             // optional: a coarse hover or click for every resolved interaction
config.clock = std::make_shared<qqs::SteadyClock>();   // optional: a test passes a clock that it advances by hand
config.pump_input_in_tick = false;          // a host whose tick is a QTimer calls pump_input() from a posted event
config.dialog_panels = true;                // a dialog of a streamed panel gets a panel of its own
config.dialog_panel_offset_m = 0.5;         // metres in X between a panel and each of its dialog panels
config.unserved_warn_seconds = 5.0;         // report a sender that has served no frame for this long; 0 switches it off
config.close_mode = qqs::PanelCloseMode::Destroy;   // what a closed window does to its AR panel
config.max_tombstoned_panels = 16;          // only for the notice mode; 0 means no limit
config.pointer_state.double_click_interval_s = 0.4;
config.pointer_state.double_click_distance_px = 5.0;
```

| Call | What it does |
|---|---|
| `add_panel(key, widget, options)` | starts streaming a widget under a key; the QarOS panel is created on the first `tick()` that finds the widget laid out, so the call never waits for QarOS; the key of a retired or parked panel is revived onto the new widget |
| `close_panel(key, reason, notice_reason)` | the one door of every close that the Qt side starts; follows the close mode; safe to call from the click handler of the panel itself |
| `remove_panel(key)` | `close_panel` with no reason |
| `tombstone_panel(key, reason)` | the notice operation, whatever the close mode is |
| `note_ar_panel_closed(key)` | the operator closed the panel in AR: stop capturing; the manager calls it itself for every report of the input source |
| `clear()` | the end of the session: closes every panel that is still held, once |
| `tick()` | drains the input unless the host pumps it, drains the closes of the operator, sweeps the dialogs, advances the fit of the dialogs and runs one capture and submit cycle for every panel; returns the number of panels that submitted a frame |
| `pump_input()` | drains the input source and processes every sample; re-entrant; returns the number of samples |
| `handle_sample(sample)`, `route_room_point(point, kind)`, `route_room_ray(origin, direction, kind)` | the host's own entry points for a pointer sample, a point or a ray; each returns whether a panel was hit |
| `set_panel_pose(key, pose)` | moves one panel |
| `set_panel_ar_visible(key, visible)` | hides a panel in AR, or shows it again, without closing it; the next `tick()` carries the request out |
| `set_close_mode(mode)` | changes what the Qt-side closes that follow do |
| `ar_panel_height_m(widget, options)` | the height in metres that the panel of a widget gets, for a host that places the panel before it exists |
| `set_dialog_size_adjuster(adjuster)`, `set_dialog_pose_adjuster(adjuster)` | the fit of a dialog before it gets a panel, and the change of the pose of its panel |
| `request_scroll_census(key, reason)` | one more census of the scroll areas of the widget of a panel, at the next frame it sends |
| `set_panel_region_source(key, region, source)`, `clear_panel_region_sources(key)` | video over a region of the widget of a panel |
| `dismiss_owned_dialogs(key)` | closes every dialog that the widget tree of the panel owns, innermost first; returns how many |
| `has_panel`, `panel_keys`, `dialog_panel_keys`, `tombstoned_panel_keys`, `is_tombstoned`, `is_parked`, `parked_panel_keys`, `is_ar_hidden`, `replaced_panel_count`, `close_mode` | queries |
| `diagnostics()`, `reset_submit_gap_windows()`, `pointer_state()` | a copy of the counters, a new window for the statistics of the gaps between frames, and the pointer state machine |

The calls return a `qar_ar_core::Result`. `add_panel` answers `InvalidArgument` for an empty key and for a null widget, `AlreadyExists` for a key that streams, and `InvalidState` when the cap of retired panels is reached for a new stream or a retired slot cannot be revived; `close_panel`, `set_panel_pose`, `set_panel_region_source` and `clear_panel_region_sources` answer `NotFound` for a key that does not stream, and so does `set_panel_ar_visible`, which answers `InvalidState` for a panel that shows the notice. In the code, `manager` is the manager of the host and `widget` the widget to stream.

```cpp
qqs::PanelStreamOptions options;
options.display_name = "Settings";                                  // the title that QarOS shows; the key when empty
options.placement.pose.position = QarVector3{0.0f, 1.5f, -1.0f};    // the top centre of the panel, in room space

const qar_ar_core::Result added = manager->add_panel("settings", widget, options);
if (!added.ok()) {
  // InvalidArgument: an empty key or a null widget. AlreadyExists: the key streams already.
  // InvalidState: the cap of the notice mode is reached.
  std::printf("add_panel: %s: %s\n", qar_ar_core::to_string(added.status), added.message.c_str());
  return;
}

// Move the panel; its size always comes from the widget.
QarPose pose = options.placement.pose;
pose.position.x = 0.5f;
manager->set_panel_pose("settings", pose);

const std::uint32_t submitted = manager->tick();       // one capture and submit cycle for every panel
const std::uint32_t samples = manager->pump_input();   // from a posted event when pump_input_in_tick is false
std::printf("%u frames submitted, %u pointer samples\n", static_cast<unsigned>(submitted), static_cast<unsigned>(samples));

manager->close_panel("settings", "the window was closed");   // the one door of every close on the Qt side
```

A sink gets the coarse result of the input, a hover or a click with the key of the panel, the position of the hit and the widget pixel. It is optional, because the manager injects every action into the widgets whether or not a sink is set:

```cpp
// A sink that prints what the manager resolved. It is optional: the manager injects every action into the widgets
// whether or not a sink is set.
class PrintingSink : public qqs::IQarPanelInputSink
{
public:
  void on_panel_pointer(const qqs::PanelPointerEvent & event) override
  {
    std::printf("%s: %s at widget pixel (%d, %d), u %.2f, v %.2f\n", event.panel_key.c_str(),
      event.kind == qqs::PanelPointerEvent::Kind::Click ? "click" : "hover", event.widget_x, event.widget_y,
      static_cast<double>(event.hit.u), static_cast<double>(event.hit.v));
  }
};
```

`diagnostics()` returns a copy of the counters, so a host reads it on the GUI thread without a lock. The snapshot holds the totals of the manager, namely the panels and the live ones, the retired, the hidden, the parked and the closed ones, the number of closes that were deferred or failed, and the pointer samples that arrived, that landed on no panel and that made no sense; and one record for each panel with the frames that it captured, submitted and skipped for each reason, the size of its buffer and panel, its stream id, the senders that were built for it, its input counters, whether it is hidden in AR or parked and the counters of its hide and show requests, the brightness of the last frame that was measured, and its last error. `reset_submit_gap_windows()` starts a new window for the gaps between the frames of a panel, which a host that logs them calls after it has read them.

```cpp
const qar_ar_core::PanelDiagnosticsSnapshot snapshot = manager->diagnostics();   // a copy, on the GUI thread
std::printf("%u panels, %u live, %llu closed\n", static_cast<unsigned>(snapshot.panel_count),
  static_cast<unsigned>(snapshot.live_panels), static_cast<unsigned long long>(snapshot.panels_closed));
for (const qar_ar_core::PanelStreamDiagnostics & panel : snapshot.panels) {
  std::printf("%s: %llu submitted, %llu unchanged, %llu not ready, %llu rebuilds, visible %d\n", panel.key.c_str(),
    static_cast<unsigned long long>(panel.frames_submitted),
    static_cast<unsigned long long>(panel.frames_skipped_unchanged),
    static_cast<unsigned long long>(panel.frames_skipped_not_ready),
    static_cast<unsigned long long>(panel.adapter_rebuilds), panel.widget_visible ? 1 : 0);
}
manager->reset_submit_gap_windows();   // start a new window for the submit_gap statistics
```

### Hiding, parking and fitting

`set_panel_ar_visible(key, visible)` hides a panel in AR, or shows it again, without closing it. It records the request on the stream and makes no call to QarOS itself: the next `tick()` carries it out through the sender of the panel, which calls `qar_gui_panels_set_state` in qar_ogre_adapter. While a panel is hidden it is not captured, it takes no input and every hit test skips it. `is_ar_hidden(key)` is the state that QarOS accepted, so a request made since the last tick is not in it yet. `is_parked(key)` is true from the Qt-side close that parked a stream, in the close mode `Park`, until a window is shown on it again, and `parked_panel_keys()` lists such streams in key order. `set_close_mode(mode)` changes what the Qt-side closes that follow do, and streams that are parked or retired already stay as they are. `replaced_panel_count()` is the number of QarOS panels that earlier size changes replaced and kept in the mode `Park`, which count against `max_replaced_panels`. A host that places a panel before it exists asks `ar_panel_height_m(widget, options)`, the height in metres that the panel of the widget gets from the manager, which is 0 for a widget that has no size yet.

A dialog that a streamed panel opens is fitted before it gets its panel. `set_dialog_size_adjuster(adjuster)` installs a `DialogSizeAdjuster`, a function that the manager calls with the dialog, the available geometry of its screen and the round, counted from 0, and that answers a `DialogFitStep`: the size of the dialog after the step, whether the dialog is done, and a note for the log. `make_census_dialog_adjuster(slack_px, max_rounds)` builds the adjuster of the library, which makes the dialog taller by the overflow of its scroll areas, and without an adjuster the fixed `dialog_height_factor` of the configuration applies. `set_dialog_pose_adjuster(adjuster)` installs a `DialogPoseAdjuster` that may change the pose of the panel of a dialog, given its top-centre pose and its height in metres. [Architecture](ARCHITECTURE.md#popups-and-dialogs) says how the rounds run.

## Capture and stream options

`PanelStreamOptions` is the policy of one panel, chosen when the widget is added. The pose in the placement is the only part of the placement that is yours, because the size of the panel is derived from the widget at every tick; a panel hangs below its pose, which is the middle of its top edge. Two fields concern the size: `ar_frame` is the frame that QarOS draws around the video, which the manager replaces with the `ar_frame` of its own configuration for every panel it makes, and `dialog_panel` marks the panel of a dialog, which keeps the height of its video and fits its width to the frame, and which the manager sets. The free function `ar_panel_height_m(widget, options)` gives the height that the panel of a widget would get. [Configuration](CONFIGURATION.md#runtime-option-structs) lists the defaults, and [Architecture](ARCHITECTURE.md#resize-and-rebuilds) explains the three limits of a resize.

```cpp
qqs::PanelStreamOptions options;
options.display_name = "Robot status";                              // the title that QarOS shows
options.placement.pose.position = QarVector3{0.0f, 1.4f, -1.2f};    // only the pose is yours; the size comes from the widget
options.meters_per_logical_px = 0.0012;      // the default: 1.2 mm in the room for each logical pixel
options.render_scale = 1.0;                  // above 1.0 puts more pixels behind the same size
options.buffer_alignment = 2;                // the buffer is rounded up to an even number of pixels
options.max_buffer_dimension = 4096;         // a larger widget streams at a lower pixel ratio
options.max_fps = 15;                        // at most this many captures a second; 0 captures on every tick
options.capture_on_frame_arrival = true;     // a panel with video is captured when a frame arrives
options.keepalive_seconds = 1.0;             // an unchanged picture is sent again after this long; 0 switches it off
options.resize_threshold_px = 8;             // a size change below this is ignored ...
options.resize_settle_seconds = 0.5;         // ... one above it has to hold for this long ...
options.resize_settle_ticks = 3;             // ... and for this many ticks before the sender is rebuilt
```

`QtPanelStream` is one widget as one panel, which the manager makes for every `add_panel`; a host that wants no manager can drive one itself with `tick(now_seconds)`, `set_pose`, `deliver_input`, `close` and the diagnostics, on the GUI thread. `apply_readability(recommendation, options)` copies the metres per logical pixel and the render scale of a readability recommendation into the options.

`QtWidgetCapture` draws one widget into a reused RGBA8888 buffer and reports whether the picture changed. It is the part to use alone when a host wants the pixels and not the streaming. The buffer is allocated once for each size; `capture(buffer_width, buffer_height, device_pixel_ratio)` returns false for a missing widget, a zero size, a ratio that is not positive or a failed allocation, with the reason in `last_error()`. `content_changed()` is true for the first capture and after every resize. The popups that the widget owns are composited on top, and the regions that were given a video source are painted first.

```cpp
qqs::QtWidgetCapture capture(widget);               // borrowed: the widget has to outlive the capture
capture.set_composite_tooltips(false);              // popups stay in the picture, tooltips are left out
capture.set_region_source(camera_view, camera);     // video over a native window that render() cannot draw

// The size of the buffer in pixels and its pixel ratio, as compute_panel_geometry gives them.
if (capture.capture(420, 200, 1.0)) {
  if (capture.content_changed()) {                  // the picture differs from the previous capture
    const qar_ar_core::PanelPixels pixels = capture.pixels();   // borrowed until the next capture
    std::printf("%u x %u pixels\n", static_cast<unsigned>(pixels.width), static_cast<unsigned>(pixels.height));
  }
  std::printf("visible %d, popups drawn %u, clipped %d\n", capture.widget_visible() ? 1 : 0,
    static_cast<unsigned>(capture.composited_popup_count()), capture.popup_clipped() ? 1 : 0);
} else {
  std::printf("capture failed: %s\n", capture.last_error().c_str());
}
```

| Member | What it tells |
|---|---|
| `pixels()` | the last frame as a `PanelPixels`, borrowed until the next capture or the destruction |
| `widget_visible()` | whether the widget was visible at the last capture; false means that the frame holds only the background |
| `last_capture_ms()` | the wall time of the last capture |
| `owned_popup_count()`, `composited_popup_count()` | the popups that the widget owns, and the ones that were drawn into the frame |
| `offscreen_popup_count()`, `popup_summary()` | how many of the popups the guard keeps off the desktop, and the class, size and position of the topmost one |
| `popup_clipped()` | whether a popup had to be cut at the edge of the panel |
| `substituted_region_count()`, `region_frame_sequence()` | the regions that were painted with video, and the sum of the frame sequences of the sources of the visible regions |
| `invalidate()`, `set_widget(widget)` | forget the previous frame, so that the next capture is reported as changed; capture another widget |

`hash_pixels(data, width, height, stride_bytes)` is the FNV-1a hash behind the dirty check, over the rows without their padding. `decide_capture(input)` of `capture_gate.hpp` is the pure decision whether a panel may capture on a tick, with the verdicts `kCapture`, `kSkipPaced` and `kSkipNoNewFrame`, and `TickIntervalEstimator` smooths the gap between ticks; both run without a widget.

**Overlay scans.** A popup, a tooltip and a dialog are top-level windows that belong to a panel by the parent walk, or by attribution for a dialog. `collect_panel_popups(root, include_tooltips)` returns the popups of the tree of `root`, parents first; `collect_panel_overlays(root, options)` generalises it with a `PanelOverlayOptions` whose fields `popups`, `tooltips`, `dialogs`, `attribute_by_input` and `decide_attribution` are all true by default; `panel_owned_modal_window(root)` returns the modal window of the tree that blocks input, or null. `PanelOverlayAttribution::instance()` holds the attribution, whose `window_ms()` is 1500 and can be set; `register_panel`, `note_panel_input` and `owner_of` are what the injector and the manager call, and `reset()` forgets every decision for a test. `is_dialog_window(widget)` tells a dialog-class window.

### The census and the brightness

`scroll_census.hpp` measures the scroll areas of a widget. `take_scroll_census(root)` returns a `ScrollCensus` with one `ScrollAreaCensus` for each area that the operator can see: its range, its viewport, whether a vertical and a horizontal bar are shown, whether it scrolls per item or per pixel, how far it overflows in pixels, and whether it scrolls by design. The property `kScrollsByDesignProperty` marks a widget, and everything under it, as one that scrolls by design, a log or a browser, and `describe_scroll_census(census)` writes the areas as one line. The call reads ranges, policies and sizes and changes nothing. A stream takes a census at its first frame and again 1.5 s later, and `request_scroll_census(key, reason)` of the manager asks for one more at the next frame, say after a tab was selected; it answers false for a panel that is not on show.

`frame_luminance.hpp` is the measure of a frame and needs no Qt. `mean_luminance(pixels, step)` is the mean of 0.299 R + 0.587 G + 0.114 B over every `step`-th pixel in x and in y of an RGBA8888 frame, and `frame_brightness(pixels, top_fraction, step)` returns a `FrameBrightness` with that mean for all sampled pixels, for those in the top rows and for the others, and the mean alpha, each from 0 to 255; a quantity that could not be measured is `kBrightnessNotMeasured`. The default sample is every `kLuminanceSampleStep`-th pixel, 8, and the top rows are the first `kBrightnessTopFraction` of the rows, 0.15.

## Input injection

`QtPanelInputInjector` delivers `PointerAction`s into the tree of a widget as Qt events, on the GUI thread. The constructor takes the widget that is the root of the tree and the frame of `PointerAction::x` and `y`, borrowed through a `QPointer`, and the `PanelInputOptions`. `deliver(action)` returns true when at least one event was delivered; otherwise it counts a drop with a reason that `stats()` keeps, `delivered`, `dropped` and `last_drop_reason`. It refuses to run off the GUI thread, and it refuses after the widget was destroyed. `reset()` cancels every pressed pointer, leaves every hovered widget, closes the popups that exist only in the AR picture and forgets all pointers. `grab_widget(pointer_id)` and `hover_widget(pointer_id)` tell where a pointer is, and `mouse_pointer(out)` which pointer owns the synthesized mouse.

```cpp
qqs::PanelInputOptions options;
options.delivery_mode = qqs::InputDeliveryMode::TouchWithMouseFallback;   // the default; MouseOnly and TouchOnly exist
options.deliver_hover = true;          // enter, leave and hover events
options.apply_focus_policy = true;     // a press gives the focus to the first widget that takes click focus
options.route_to_popups = true;        // an open popup takes every pointer
options.block_behind_modal = true;     // while a modal dialog of the panel is up, its pointers reach nothing

qqs::QtPanelInputInjector injector(widget, options);   // on the GUI thread
qqs::PointerAction press;
press.kind = qqs::PointerActionKind::Press;
press.pointer_id = 1;
press.x = 40.0;                        // logical pixels of the widget
press.y = 12.0;
if (!injector.deliver(press)) {
  std::printf("dropped: %s\n", injector.stats().last_drop_reason.c_str());
}
injector.reset();                      // cancel every pressed pointer and leave every hovered widget
```

| Field of `PanelInputOptions` | Default | What it does |
|---|---|---|
| `enabled` | true | false makes a panel that streams out and takes no input |
| `delivery_mode` | `TouchWithMouseFallback` | `MouseOnly`, `TouchOnly` or `TouchWithMouseFallback`; [Architecture](ARCHITECTURE.md#input-path) says what each does |
| `deliver_hover` | true | enter, leave and hover events and button-less moves; switch it off for a display-only panel |
| `apply_focus_policy` | true | a press gives the focus to the first widget up the chain whose focus policy includes click focus, which is what makes a clicked line edit take typing |
| `route_to_popups` | true | while a popup is open, the pointer goes to it, and a press outside it is how it closes |
| `block_behind_modal` | true | while an application-modal window of the panel is up, the pointers outside it reach nothing |

An action produces no event for one of these reasons, and the last one is in `stats().last_drop_reason`:

| Reason | When |
|---|---|
| "input is disabled for this panel" | `PanelInputOptions::enabled` is false |
| "not on the Qt GUI thread that owns the panel widget" | the call came from another thread |
| "panel widget destroyed", "no QApplication" | the widget is gone, or no application object exists |
| "blocked by a modal dialog owned by this panel" | a press, drag or release while a modal dialog of the panel is up |
| "no widget under the pointer accepts touch events (touch-only mode)" | `TouchOnly`, and nothing accepted the touch |
| "a second pointer pressed while another one owns the synthesized mouse" | a second pointer on a plain widget |
| "release without a press", "cancel without a press" | the sequence of a pointer is out of order |
| "grabbed widget destroyed during a drag", "grabbed widget destroyed before its release" | the widget under the grab was destroyed |

`PanelPointerStateMachine` is the Qt-free half: `process(sample, out)` turns a `ResolvedPointerSample`, the sample after the geometry placed it on a panel, into the `PointerAction`s of [Architecture](ARCHITECTURE.md#input-path), appended in delivery order. `is_pressed`, `grabbed_panel` and `hovered_panel` tell the state of a pointer, `forget_panel(key, out)` cancels and leaves what a vanished panel held, `reset(out)` drops every pointer, and `diagnostics()` counts the samples, those that landed on no panel, those that made no sense, the actions, the native tap counts and the long presses. The configuration, `PointerStateConfig`, holds the double click interval, 0.4 s, and distance, 5 logical pixels.

```cpp
qqs::PanelPointerStateMachine machine;            // no Qt needed
qqs::ResolvedPointerSample sample;
sample.pointer_id = 1;
sample.gesture = qar_ar_core::PointerGesture::Click;
sample.phase = qar_ar_core::PointerPhase::Instant;   // a tap: a press, a release and a click in one sample
sample.timestamp_s = 12.0;
sample.target.panel_key = "settings";
sample.target.x = 40.0;
sample.target.y = 12.0;
sample.target.inside = true;

std::vector<qqs::PointerAction> actions;
machine.process(sample, actions);                 // Enter, Press, Release and Click, in this order
for (const qqs::PointerAction & action : actions) {
  std::printf("%s at (%.0f, %.0f)\n", qqs::to_string(action.kind), action.x, action.y);
}
```

## Readability and geometry

The functions of these two headers are pure: they need no `QApplication`, so a host can size a panel before it creates a widget. `recommend_readability(profile, out, error)` derives the metres per logical pixel and the render scale from a viewing distance; it returns false, with the reason in `error`, when a field of the profile is not finite or not positive, when `cap_height_ratio` is not in (0, 1], or when `max_render_scale` is below 1, and it leaves `out` untouched then. `readability_preset` gives the profile of `Near`, `ArmsLength` or `WallMounted`, which differ only in the viewing distance, `subtended_arcmin(length_m, distance_m)` is the angle of a length at a distance, 0 for a distance that is not positive, and `ceil_to_half` rounds up to the next multiple of 0.5. [Architecture](ARCHITECTURE.md#dpi-buffers-and-ar-readability) has the formulas and the defaults of the profile.

```cpp
qqs::ReadabilityProfile profile = qqs::readability_preset(qqs::ReadabilityPreset::ArmsLength);   // 0.65 m
profile.body_font_px = 14.0;               // the body font that the widgets use
profile.device_pixels_per_degree = 47.0;   // the resolution of the headset

qqs::ReadabilityRecommendation recommendation;
std::string error;
if (qqs::recommend_readability(profile, recommendation, error)) {
  qqs::apply_readability(recommendation, options);   // the metres per logical pixel and the render scale
  std::printf("%.3f mm per logical px, render scale %.1f, no text under %.1f px\n",
    recommendation.meters_per_logical_px * 1000.0, recommendation.render_scale,
    recommendation.min_legible_font_px);
} else {
  std::printf("profile refused: %s\n", error.c_str());
}
```

`compute_panel_geometry(input, out, error)` turns the logical size of a widget, its pixel ratio and the physical size of a pixel into the size of the buffer and of the panel. It returns false for a zero logical size, which is a widget that is not laid out yet, for a pixel ratio, a metres value or a render scale that is not finite and positive, and when the maximum buffer size is smaller than the alignment; a `max_buffer_dimension` of 0 means the default of 4096. `PanelGeometry` carries `buffer_width` and `buffer_height`, `content_width` and `content_height`, `width_meters` and `height_meters`, which are what is sent to QarOS, `nominal_width_meters`, `video_width_meters` and `video_height_meters`, `frame`, `narrowing`, `content_scale`, which is always 1.0, and `effective_pixel_ratio`. `align_up(value, alignment)` rounds up and saturates instead of wrapping.

```cpp
qqs::PanelGeometryInput input;
input.logical_width = 420;           // QWidget::width()
input.logical_height = 200;          // QWidget::height()
input.device_pixel_ratio = 1.5;      // QWidget::devicePixelRatioF()
input.render_scale = 2.0;            // the supersample of the readability recommendation

qqs::PanelGeometry geometry;
std::string error;
if (qqs::compute_panel_geometry(input, geometry, error)) {
  // The ratio is 1.5 * 2.0 = 3.0: a buffer of 1260 x 600 pixels, a panel of 0.504 x 0.309 m and a content scale of 1.0.
  std::printf("buffer %u x %u px, panel %.3f x %.3f m, content scale %.1f\n",
    static_cast<unsigned>(geometry.buffer_width), static_cast<unsigned>(geometry.buffer_height),
    static_cast<double>(geometry.width_meters), static_cast<double>(geometry.height_meters),
    static_cast<double>(geometry.content_scale));
} else {
  std::printf("no geometry: %s\n", error.c_str());   // for example a widget that is not laid out yet
}
```

`PanelFrame` is the frame that QarOS draws around the video of a stream panel: `top_m`, `bottom_m`, `left_m`, `right_m` and `fit_margin_m`, in metres, all finite and not negative, and `vertical_m()` and `horizontal_m()` give the sums of the insets. The defaults, `kDefaultArFrameTopM` 0.066 and 0.003 for the other three insets and for the margin, are the frame of the QarOS desktop Visualizer; all zeros is no frame. `PanelGeometryInput::frame` is the frame that the geometry uses, and `narrow_for_frame` makes the panel of a dialog keep its height and fit its width instead. In the result, `nominal_width_meters` is the width the widget asks for, `video_width_meters` and `video_height_meters` are the size at which QarOS draws the video, `frame` is the frame that the sizes were made with, which is all zeros when it could not be applied, and `narrowing` is `FrameNarrowing::None`, `Reduced`, `Limited` or `Skipped`. A dialog panel is never narrower than `kMinNarrowedWidthShare`, half, of its nominal width. [Architecture](ARCHITECTURE.md#dpi-buffers-and-ar-readability) has the formulas and says why the panel is made a fit margin taller than its video.


`panel_hit_test.hpp` maps the room to the pixels of a panel. `hit_test_panel_point(placement, point)` projects a point of the room onto the plane of the panel, and `intersect_panel_ray(placement, origin, direction)` intersects a ray with it, answering `valid` false for a ray that is parallel to the plane or whose plane lies behind its origin; `distance_m` is the signed distance along +Z of the panel for a point, and for a ray how far along the ray the plane was met. `panel_uv_to_widget_px(hit, width, height, x, y)` gives the widget pixel, clamped to the last pixel, and false outside the panel; `panel_hit_to_widget_point` gives the position as real numbers, unclamped, for a hit that needs only to be valid; `panel_uv_to_room_point` is the inverse of the point hit. `pick_panel_for_point` chooses the nearest panel of a vector of `PanelHitCandidate`s, and `pick_panel_for_ray` the first one along the ray.

```cpp
const QarVector3 aim{0.1f, 1.45f, -1.0f};                     // a point in room space
const qqs::PanelHit hit = qqs::hit_test_panel_point(placement, aim);

std::int32_t x = 0;
std::int32_t y = 0;
if (hit.valid && qqs::panel_uv_to_widget_px(hit, 420, 200, x, y)) {   // false outside the panel
  std::printf("widget pixel (%d, %d), %.2f m from the plane of the panel\n", x, y, static_cast<double>(hit.distance_m));
}

// A ray instead of a point: the hit is where the ray meets the plane, and distance_m is how far along the ray that is.
const qqs::PanelHit along =
  qqs::intersect_panel_ray(placement, QarVector3{0.0f, 1.5f, 0.0f}, QarVector3{0.0f, 0.0f, -1.0f});
std::printf("ray: valid %d, inside %d\n", along.valid ? 1 : 0, along.inside ? 1 : 0);
```

## Off-screen panels

`show_offscreen(widget, width, height)` makes a parentless widget a window that Qt treats as shown and that never reaches the desktop, and shows it; call it once, on the GUI thread, on a widget that has not been shown. It returns false and does nothing for a null widget and for a widget with a parent, clamps each side to the range from `kMinOffscreenSide`, 200, to `kMaxOffscreenSide`, 2000, installs the popup guard first, and leaves the widget a plain `Qt::Window`. Calling it again is harmless and is how a hidden widget is brought back. `is_offscreen_panel(widget)` tells whether a widget is in the state that it leaves. [Architecture](ARCHITECTURE.md#off-screen-panels) says what each step is for.

`parse_offscreen_size(value, default_width, default_height, source)` is the pure parser of a size that an operator may choose, `WIDTHxHEIGHT` in decimal digits with `x` or `X` and optional spaces around the value and the `x`. It never fails: it always returns a usable size in an `OffscreenSizeChoice`, whose `explicitly_chosen` says that the text was a size, `clamped` that a side was moved into the range, and `warning` is the one line to print. A null pointer, an empty text and a text of blanks mean unset and give the default, 480 by 720, with no warning. Anything else that is not exactly two unsigned decimal numbers joined by an `x` gives the default and a warning that names the text as typed, for example `480*720`, and a size outside the range is clamped and reported. A number longer than an `int` saturates, so `99999999999x720` is 2000 by 720.

```cpp
// size_text is the text of a setting of your own, WIDTHxHEIGHT, or null when nothing is set.
const qqs::OffscreenSizeChoice choice = qqs::parse_offscreen_size(size_text, 480, 720, "the size of the hidden panel");
if (!choice.warning.empty()) {
  std::printf("%s\n", choice.warning.c_str());   // not a size, or a side outside 200 to 2000 that was clamped
}
// Shown to Qt and never to the desktop; false for a null widget and for a widget that has a parent.
if (qqs::show_offscreen(hidden, choice.width, choice.height)) {
  manager->add_panel("hidden", hidden, qqs::PanelStreamOptions{});
}
std::printf("off-screen: %d\n", qqs::is_offscreen_panel(hidden) ? 1 : 0);
```

The guard is installed by `show_offscreen`, and a host rarely calls it: `install_offscreen_popup_guard()` installs it once for each `QApplication` and returns false without an application or off its thread, `offscreen_popup_guard_installed()` asks, and `offscreen_popup_guard_stats()` counts the popups and the animation windows that were kept off the desktop since the process started. `popup_owner_widget(popup)` is the parent of a popup or, for a parentless one, its focus proxy; `is_owned_by_offscreen_panel(window)` and `is_offscreen_popup(widget)` answer the two questions of the guard; `close_offscreen_popups(panel_root)` closes the kept-off popups of a tree, innermost first, and returns how many. `remove_offscreen_popup_guard()` takes the guard off the `QApplication` and deletes it, and `show_offscreen` installs it again the next time; it returns false when no guard is installed and off the thread of the guard. The guard also removes itself when the module that holds this library is unloaded or the process exits, so that its code never outlives that module.

## Close modes

`PanelCloseMode` has three values. `Destroy` is the default: the AR panel is closed, its video sender destroyed and the stream forgotten. `Notice` keeps the stream and its sender and shows a notice until the session ends. `Park` closes nothing while the session runs: the AR panel is hidden and parked, and its QarOS panel, its sender and its subscriptions are kept until the session ends. `to_string(mode)` gives `destroy`, `notice` or `park`. `parse_panel_close_mode(value)` is the pure parser of the word that chooses between them: it ignores case and the blanks around the word, a null pointer, an empty text and a text of blanks give the default with no warning, and any other text gives the default and a warning that names the text and the three words. `kPanelCloseModeEnv` is the name `QAR_PANEL_CLOSE_MODE`, which a host may use for its setting: the library itself does not read it.

```cpp
// mode_text is the text of a setting of your own, or null when nothing is set. The library reads no environment
// variable itself; kPanelCloseModeEnv only names the variable that a host may use for it.
const qqs::PanelCloseModeChoice choice = qqs::parse_panel_close_mode(mode_text);
if (!choice.warning.empty()) {
  std::printf("%s\n", choice.warning.c_str());   // a word that is neither destroy nor notice
}
config.close_mode = choice.mode;                 // PanelCloseMode::Destroy or PanelCloseMode::Notice
std::printf("%s: %s (%s)\n", qqs::kPanelCloseModeEnv, qqs::to_string(choice.mode),
  choice.explicitly_chosen ? "chosen" : "default");
```

The mode is a field of the configuration of the manager, `close_mode`, and `close_mode()` reads it back. [Architecture](ARCHITECTURE.md#closing-panels) says what each mode does. The calls that close are these:

```cpp
manager->close_panel("settings", "the window was closed");    // follows the close mode of the manager
manager->remove_panel("settings");                            // the same, with no reason given
manager->tombstone_panel("status", "the window was closed");  // the notice, whatever the close mode is
if (manager->is_tombstoned("status")) {
  std::printf("%u panels show a notice\n", static_cast<unsigned>(manager->tombstoned_panel_keys().size()));
}
manager->note_ar_panel_closed("status");   // the operator closed the panel in AR; a source of input reports it for you
manager->clear();                          // end of the session: closes every panel that is still held, once
```

The notice of the second mode is built from `PanelTombstoneText`, which holds the name of the window that closed and a short reason, and from `PanelTombstoneStyle`: the body size of the text, 12 logical pixels, the minimum, 8.7 pixels, and four colours that follow the dark theme of the web panels by value, background `#0F141A`, text `#F2F5F7`, muted text `#A9B4BF` and the accent of the rule `#FFC857`. `make_panel_tombstone_widget(text, style, width, height)` returns the owned, parentless, never-shown widget, `tombstone_body_font_px(width, longest_line_chars, style)` is the pure fit, and `tombstone_is_clipped` tells whether the text does not fit even at the minimum.

## Video frames

`decode_raw_image(view, max_size, out, error)` decodes a `RawImageView`, the width, the height, the step, the encoding name, the byte order and the data of a camera image, into an opaque `Format_RGBA8888` image, and `decode_compressed_image(format, data, size, max_size, out, error)` does the same for PNG and JPEG. Both return false on a failure and leave `out` untouched, with the reason in `error` when it is not null; an empty `max_size` means no limit. `aspect_fit_rect(source, bounds)` is the largest rectangle with the aspect ratio of the source that fits the bounds, centred, and empty when either is empty. Both decoders touch only `QImage` and are safe on a worker thread. [Architecture](ARCHITECTURE.md#video-in-panels) lists the encodings and what each conversion does.

`decode_compressed_image` does not trust its input. Only PNG and JPEG are decoded; the format is decided by the first bytes of the payload (the `format` string is used in messages only); any other format is refused, whatever Qt plugins are installed. Qt can read other formats, but for some of them, GIF and ICO among them, the size in the header is not the size that Qt allocates, so the cap below could not bound them. The function reads the declared size from the header before it decodes anything, and it returns false, with the reason in `error`, for a payload of more than 2,147,483,647 bytes, the largest length that an `int` holds, and for an image that declares more pixels than a cap. The cap is 64 million pixels without a size limit. With a size limit it is 16 times the area of `max_size`, but at least 16 million and at most 64 million, so that an ordinary camera frame is accepted for any size limit. Qt reads and writes JPEG through its plugin `qjpeg.dll`, which it finds in a folder named imageformats next to the program, so a program that decodes JPEG ships that plugin; without it a JPEG is not decoded. The tests of this repository stage the plugin next to the test executable.

```cpp
// On the worker thread of the host, never on the GUI thread.
qqs::RawImageView view;
view.width = width;
view.height = height;
view.step = width * 3;                               // bytes in a row, any padding included
view.encoding = "rgb8";
view.data = rgb;
view.size = static_cast<std::size_t>(view.step) * height;

QImage image;
std::string error;
if (qqs::decode_raw_image(view, QSize(1280, 720), image, &error)) {   // scaled down to fit, never up
  mailbox->publish(image);                           // the GUI thread only reads
} else {
  mailbox->set_status(error);                        // shown in place of a frame
}

// A compressed image: only PNG and JPEG are decoded, the first bytes decide, and the format text is used in messages only.
if (qqs::decode_compressed_image("jpeg", jpeg, jpeg_size, QSize(), image, &error)) {   // an empty size is no limit
  mailbox->publish(image);
}
```

`LatestVideoFrame` is the mailbox between a decode worker and the GUI thread, and every member takes the same mutex:

| Member | What it does |
|---|---|
| `publish(frame)`, `publish(frame, trace)` | replaces the frame, counts it, updates the rate and clears the status; safe from any thread |
| `latest()` | a `std::shared_ptr<const QImage>` to the newest frame, or null before the first `publish` |
| `sequence()` | the number of frames that were published; it only grows |
| `fps()` | the smoothed rate of `publish`, 0 before the second frame |
| `set_status(text)`, `status()` | the text that a reader shows in place of a frame; empty is healthy |
| `latest_trace()` | the `VideoFrameTrace` that was stored with the newest frame, with `valid` false when none was given |

`VideoFrameTrace` records where a frame was on its way from the callback of the host to the mailbox, in microseconds on the system clock, and is filled only while per-frame timing is on. `VideoPanelWidget(source, title, logical_width)` is the content of a stand-alone video panel: the widget that paints the newest frame of a `LatestVideoFrame`, with a status text before the first frame and a caption strip. It is parentless and shown to Qt only, the host owns it, `sync_to_frame()` adapts its height to the aspect ratio of the frames, once for each tick on the GUI thread, `painted_sequence()` is the sequence that the last paint showed, and `source()` is its mailbox.

```cpp
// view is made by the host, as qqs::VideoPanelWidget(mailbox, "Front camera", 640): parentless, shown to Qt only.
qqs::PanelStreamOptions options;
options.max_fps = 30;                                // the upper bound; the panel is captured when a frame arrives
manager->add_panel("video:front", view, options);

// In the tick of the host, before manager->tick(): follow the aspect ratio of the frames.
if (view->sync_to_frame()) {
  std::printf("the height follows the frames: %d px\n", view->height());
}
std::printf("frames published: %llu, %.1f per second\n", static_cast<unsigned long long>(mailbox->sequence()),
  mailbox->fps());
```

Video can also replace a region of a larger widget, such as an embedded native window; the manager takes the region and its source in `set_panel_region_source`:

```cpp
// render_view is a child of the streamed widget that embeds a native window; QWidget::render() cannot paint it.
const qar_ar_core::Result set = manager->set_panel_region_source("dock", render_view, mailbox);
if (!set.ok()) {
  std::printf("no such panel: %s\n", set.message.c_str());   // NotFound for a key that does not stream
}
manager->clear_panel_region_sources("dock");           // stop painting video over every region of the panel
```

## Thread rules

| Call | Thread | Blocking | Failure is reported as |
|---|---|---|---|
| every method of `QtPanelStreamManager` and `QtPanelStream` | the Qt GUI thread | `tick()` captures every panel that passes its gate, and the tick that creates a panel and `close_panel` call the C API synchronously; `add_panel` never calls it | a `qar_ar_core::Result`, a `bool`, or a line to the diagnostics sink |
| `QtPanelInputInjector::deliver`, `reset` | the Qt GUI thread; another thread is refused | none | `false`, with the reason in `stats()` |
| `QtWidgetCapture::capture` | the Qt GUI thread | a full repaint of the widget | `false`, with the reason in `last_error()` |
| `show_offscreen`, `install_offscreen_popup_guard`, `remove_offscreen_popup_guard`, `VideoPanelWidget::sync_to_frame` | the Qt GUI thread | none | `false` |
| `PanelDiagnosticsSink::on_message`, `IQarPanelInputSink::on_panel_pointer` | called on the Qt GUI thread | must not block | none |
| `LatestVideoFrame` | any thread | one short mutex | none |
| `decode_raw_image`, `decode_compressed_image` | any thread; meant for a worker | the CPU time of the decode | `false`, with the reason in `error` |
| the parsers, the geometry, the readability, the hit test, `PanelPointerStateMachine`, `decide_capture` | any thread | none | a value, or `false` with a reason |

[Thread model](ARCHITECTURE.md#thread-model) explains why the rules are what they are.

## What the package installs

```powershell
Get-ChildItem -Recurse -File "$env:QAR_PREFIX" | Where-Object { $_.FullName -like '*qar_qt_streaming*' } | ForEach-Object { $_.FullName.Substring("$env:QAR_PREFIX".Length + 1) }
```
```text
include\qar_qt_streaming\ar_readability.hpp
include\qar_qt_streaming\capture_gate.hpp
include\qar_qt_streaming\clock.hpp
include\qar_qt_streaming\latest_video_frame.hpp
include\qar_qt_streaming\offscreen_panel.hpp
include\qar_qt_streaming\offscreen_popup_guard.hpp
include\qar_qt_streaming\panel_close_mode.hpp
include\qar_qt_streaming\panel_geometry.hpp
include\qar_qt_streaming\panel_hit_test.hpp
include\qar_qt_streaming\panel_pointer_state.hpp
include\qar_qt_streaming\panel_tombstone.hpp
include\qar_qt_streaming\qt_panel_input_injector.hpp
include\qar_qt_streaming\qt_panel_stream.hpp
include\qar_qt_streaming\qt_panel_stream_manager.hpp
include\qar_qt_streaming\qt_widget_capture.hpp
include\qar_qt_streaming\video_frame.hpp
include\qar_qt_streaming\video_panel_widget.hpp
lib\qar_qt_streaming.lib
share\qar_qt_streaming\LICENSE
share\qar_qt_streaming\cmake\qar_qt_streamingConfig.cmake
share\qar_qt_streaming\cmake\qar_qt_streamingConfigVersion.cmake
share\qar_qt_streaming\cmake\qar_qt_streamingTargets-relwithdebinfo.cmake
share\qar_qt_streaming\cmake\qar_qt_streamingTargets.cmake
```

The command lists the files of this package below the install prefix, relative to it. The package puts the headers below `include`, the static library below `lib`, and the CMake package files and the licence below `share/qar_qt_streaming`; the vendored QarOS header and its licence belong to the package qar_ogre_adapter.
