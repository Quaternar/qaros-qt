# QarOS SDK compatibility

## Supported QarOS SDK drops

A QarOS SDK drop has no version number: its C header carries no version macro, and its libraries carry no version resource. QuaternAR names a drop by its date, and this stack states the drops it supports by those dates.

| SDK drop | Status | Meaning |
|---|---|---|
| 2026-09-28 | verified | The drop that this stack was built, tested and run against. The vendored C header is the header of this drop. |
| 2026-09-21 | minimum | The earliest drop that provides everything the code calls. The four items below set this minimum. |
| earlier drops | not supported | They lack at least one of the four items below. |

The minimum is set by these parts of the C API, all of which the code uses and which the vendored header declares:

- the function `qar_session_id_unique`, which makes a session id that is unique and not zero;
- the function `qar_gui_panels_subscribe_all_gesture_events`, which subscribes to all gesture events of a GUI panel;
- the parameter structure `QarStreamParamsOpenGlWgl`, which hands the Windows OpenGL context of the host to a render stream;
- the field `app_volume_id` of `QarRenderSenderInit`, which must not be NULL: a NULL id is rejected with the status `QAR_STATUS_APP_VOLUME_INVALID_ID`.

At the time of writing, the public QarOS repository <https://github.com/Quaternar/qaros> publishes a later generation of the C API. This stack does not build against it: the session set-up (create, serialise, join and invite), the hand of a gesture, the time points and the init structures of volumes and panels would have to be ported.

## Identifying an SDK drop

Because a drop has no version number, you identify it by the SHA256 of its C header, the file `include\qar_streaming.h` in the SDK folder. Two drops with the same header have the same C API; the hash does not prove that every binary of the two drops is identical.

The first command prints the hash of the header in your SDK folder.

```powershell
(Get-FileHash -LiteralPath (Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

The header that this stack compiles against is the header of the verified drop, and the second command prints its hash.
The repository qar_ogre_adapter installs the header with its libraries, so the command reads it from the install prefix.

```powershell
(Get-FileHash -LiteralPath (Join-Path $env:QAR_PREFIX 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

If the two hashes are equal, your SDK has the C API of the verified drop. If they differ, your drop is another one. The third command checks whether it still provides the four items that set the minimum drop; it prints True for each item that the header of your SDK declares.

```powershell
$header = Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h'
foreach ($name in 'qar_session_id_unique', 'qar_gui_panels_subscribe_all_gesture_events', 'QarStreamParamsOpenGlWgl', 'app_volume_id') {
    '{0,-46} {1}' -f $name, (Select-String -LiteralPath $header -Pattern $name -SimpleMatch -Quiet)
}
```
```text
qar_session_id_unique                          True
qar_gui_panels_subscribe_all_gesture_events    True
QarStreamParamsOpenGlWgl                       True
app_volume_id                                  True
```

A drop that has all four items and a different header is not verified: the stack may work with it, but nobody has checked. Build the repositories, run the tests, and read the changes listed for each drop before you rely on it.

## Updating the QarOS header

This repository does not hold the QarOS C header. It comes with the repository qar_ogre_adapter, which vendors it and installs it into the prefix next to its libraries, so that every repository of this stack compiles against the same file. To move to another SDK drop, update the header there first, following the document "SDK compatibility" of qar_ogre_adapter, install qar_ogre_adapter into the prefix again, and then rebuild this repository.

### Rebuild order

After the header has changed, rebuild the repositories in the order of their dependencies: qar_ogre_adapter first, then qar_qt_streaming, and qar_rviz_plugin last. Each repository is built and installed into the prefix as described in its installation document, and the later ones find the new header and libraries there.

Rebuild from scratch rather than incrementally: an incremental build can keep objects that were compiled against the old header.

Go to the folder of the clone of this repository first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
cmake --build --preset vs2019-x64 --clean-first
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

## API changes by SDK drop

The table lists the changes of the C header from the minimum drop to the verified drop, grouped by drop. The rows of the minimum drop say what its header declares and what this stack needs from it; the rows of the later drops are the differences between the headers of the drops. The drop that lies between the minimum drop and the verified drop is called "after the minimum drop". Only the rows that concern this repository are shown. In the third column, the adapter is the QarOS C API adapter in qar_ogre_adapter:

| SDK drop | Change in the header | Effect on this stack |
|---|---|---|
| 2026-09-21 (minimum drop) | Panel input is a gesture stream: the per-kind click and hover subscriptions and their flat pointer event are replaced by `qar_gui_panels_subscribe_all_gesture_events`, which takes every kind through one reader, and by the opaque handle `QarGuiPanelGesture`, which is read through accessors. The kinds are hover, tap, long press and drag, each with a phase. Every panel, URL panels included, reports gestures. | The adapter subscribes once per stream panel and reads the handle at the top of the callback. |
| 2026-09-21 (minimum drop) | A panel gesture names its hand, `qar_gui_panel_gesture_get_hand`, and counts its taps, `qar_gui_panel_gesture_get_tap_count`. A position, `QarGuiPanelPoint`, holds signed integers. | Every hand of every peer is its own pointer with its own grab, and the adapter reads the tap count. |
| 2026-09-21 (minimum drop) | The header documents the frame of a panel: the origin is the midpoint of the top edge, +X runs to the viewer's left, the front normal is -Z, and panel content space runs backwards along both panel axes. | The pose of a panel is passed to the header unchanged, and the hit test of the Qt library uses this frame. |
| 2026-09-28 (verified drop) | A stream panel ignores `content_scale`: its video is drawn at the full panel width and scrolled vertically when it is taller than the panel. | This stack sends 1.0 by default, so its panels are not affected. |
| 2026-09-28 (verified drop) | The position of a gesture on a stream panel refers to the video as its sender draws it, at the resolution of the sender, and for a video that the renderer has scrolled it is the pixel under the finger. | The adapter normalises the position against the content size that its caller names. |

The SHA256 of the file `include\qar_streaming.h` tells the three headers apart:

| SDK drop | SHA256 of the header |
|---|---|
| 2026-09-21 (minimum drop) | D26892604A186E5CC0AEC149614410F7DD648570FE28510A9D49C65220034C50 |
| after the minimum drop | BF0C8C2DE23584B02E7CC1E96B66282908792477FB48C7990CF7AE4E1E78D0FE |
| 2026-09-28 (verified drop) | 5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA |

## QarOS behaviour this repo relies on

This repository relies on the QarOS behaviour for GUI panels that the sections below state. Three statements are specific to this library and tell what its code depends on:

- **The panel gesture API needs the drop of 2026-09-21 or later.** The input of a panel comes from one subscription with `qar_gui_panels_subscribe_all_gesture_events`, which the adapter of qar_ogre_adapter makes for every panel, and that function is one of the four items that set the minimum drop. A drop before it does not declare that function, and this stack does not support such a drop.
- **Destroying the sender of a closed panel is the default, and the current runtime supports it.** QuaternAR states that the current runtime survives the destruction of a stream while other streams run, so closing a window closes its AR panel and destroys its video sender. The header does not document this, and the statement is that of the vendor; the code relies on it in the default mode of `panel_close_mode.hpp`. The mode `notice` is the fallback for a runtime on which it is not safe: it keeps the stream and its sender until the session ends. [Closing panels](ARCHITECTURE.md#closing-panels) describes both modes.
- **The C API does not tell a panel-only program where the operator is.** The pose of the viewer, its head and its eyes, comes from the frame information of a render sender, in the app content space of the volume that the sender is bound to, and the hands from the same sender; a program that streams only panels has no render sender, and the library has no input for one. A dialog panel is therefore placed beside its owner and not in front of the operator.

### Panel pixel coordinates and content_scale

A position on a GUI panel is reported in panel content space, the pixel raster that is drawn onto the panel. The origin is the top-left corner of the displayed image as the viewer sees it, x increases to the right and y increases downward. `QarGuiPanelPoint` holds two signed 32-bit integers, `x_pixels` and `y_pixels`, and both are clamped to the image that the renderer is currently displaying: x is in 0 to content width - 1 and y in 0 to content height - 1.

For a stream panel that image is the video exactly as its sender draws it: the renderer receives the video at the resolution of the sender, and when the renderer has scrolled the video, the position is the pixel under the finger and not a position relative to the visible part. The receiver resolution of a stream panel is taken from the layout of the video sender.

The field `content_scale` of `QarGuiPanelSize` is a zoom of the rendered raster: 1.0 fits the content across the full panel surface, 2.0 shows it at twice the size so that only half of it fits, and 0.5 shows it at half size. It changes how the content is drawn and how a contact maps back to content pixels, and it does not change `width_meters` or `height_meters`. A stream panel ignores it: its video is always drawn at the full panel width and is scrolled vertically when it is taller than the panel.

This stack sends 1.0 as `content_scale` for the panels of the Qt library and by default for URL panels, and never a resolution or pixel ratio, because a value above 1.0 would crop the content and push the position of a contact off its target. The panel of a Qt widget may be rendered at a higher pixel density than its logical size; that ratio stays on this side and is not sent.

The adapter turns a reported pixel into a position on the panel in four steps. It adds 0.5 to x and y, so that the centre of the pixel is addressed, and it divides by the content width and height that the caller of the adapter names. It flips no axis, because the origin of the header, the origin of the panel seam and the origin of Qt are all top-left with y down, and it does not apply `content_scale` again, because the renderer has already mapped the contact through it into content space. A position that lies outside the content, for example after the panel was resized under a live subscription, is clamped to the edge.

The content size is the part of the buffer that the widget covers. This stack aligns the buffer of a panel up to an even number of pixels as a precaution, because video encoders widely reject odd dimensions; the few slack pixels are background, and the position of a contact is normalised against the content size and not against the buffer.

### Panel space

The API of GUI panels is expressed in three frames. Every `QarPose` of this API, the pose of `QarGuiPanelInit`, the pose that `qar_gui_panels_update_pose` sets and the pose that `qar_gui_panel_get_pose` returns, is given in room space, the shared tracking space of the session: right-handed, in metres, +Y up. This stack states the pose of a panel in this frame and passes it unchanged to `QarGuiPanelInit`.

Panel space is the frame that this pose defines. Its origin is the midpoint of the top edge of the panel, the top centre and not a corner, and its axes are right-handed, as a viewer who faces the panel sees them: +X runs along the top edge to the viewer's left, +Y runs up, and -Z is the normal of the front face and points at the viewer. The front normal is -Z and not +Z, because a +Z normal that points at the viewer would not be right-handed. The surface of the panel covers x from -width/2 to +width/2 and y from -height to 0, so a panel hangs below its own pose.

Panel content space is the pixel raster that is drawn onto the surface. Its origin is the top-left corner of the panel as the viewer sees it, +X runs right and +Y runs down, and it is the frame in which `QarGuiPanelPoint` reports positions. It runs backwards along both panel axes: content x = 0 sits at panel-space +width/2 and content y = 0 at panel-space 0, so content x advances along panel-space -X and content y along panel-space -Y.

### Panel input lifecycle

The input of a GUI panel is one stream of gestures: hover, tap, long press and drag. Each gesture carries a phase, one of started, updated, ended, instant and canceled, and a gesture id that ties a started phase to its updates and its end. The peer that draws the panel recognises the gestures from the hand joints of its user, with thresholds that are physical, in millimetres of hand travel against tracking jitter, which a consumer that sees only content pixels cannot reproduce. Every panel reports gestures, URL panels included.

A hover is a lifecycle: started on entry of the fingertip into the hover volume, updated as the content position changes, ended on exit, and it stays open while the same fingertip touches, so that a highlight under the finger survives a press. A tap is instant: the contact was touched and released with little travel and before the long-press duration. A long press is instant and comes once per contact, and the contact may still become a drag afterwards. A drag is a lifecycle: started when the contact crosses the tap threshold, updated for every changed content position, ended on release and canceled when the contact is taken away.

One subscription delivers one kind, and several per-kind subscriptions are separate readers that have no order relative to each other. `qar_gui_panels_subscribe_all_gesture_events` delivers every kind through one reader, so a hover and the drag that grows out of the same contact keep the order in which the producer published them; the header recommends it whenever more than one kind is taken, and says to reconcile on the gesture id, which is shared across kinds within a source peer. This stack subscribes once per panel with the all-kinds subscription.

The gesture that is handed to the callback is an opaque handle to storage that belongs to the library, and it is valid only for the duration of the call: read what you need through the `qar_gui_panel_gesture_get_*` accessors and copy it. The callback runs on a background thread of the library, so it must not block and must not call a subscribe function. This stack reads everything from the handle at the top of the callback and queues samples for the GUI thread.

Several producers publish on the topic of one panel, one per user. The gesture id is unique only within a source peer, so a lifecycle is keyed by the source peer and the gesture id together, as `qar_gui_panel_gesture_get_lifecycle` returns them; the first drags of two users both carry gesture id 1. The source peer is the peer whose hand produced the gesture and not the peer that recognised it. The gesture id is 0 for tap and long press, which are instant. A live lifecycle is republished every 250 ms even when nothing changed, and a lifecycle that is silent for three of those intervals is to be treated as canceled; that, and not the peer list, recovers a producer that crashed. A started phase that was never seen is recoverable, because every update carries its start position. This stack keys on the source peer, the gesture id and the kind, and it cancels a lifecycle after 0.75 s of silence.

A contact that lies outside the letterboxed content rectangle cannot start a lifecycle, but a live lifecycle follows the clamped pixel to the edge and stays there, so a drag that overshoots commits its end value. For the instant kinds the current position and the start position are equal. The timestamp of a gesture is on the clock of the recogniser and is comparable only within one source peer id, so this stack timestamps an event with its own monotonic clock when it arrives. The hand of a gesture is diagnostic, because only one hand of a peer is active at a time; this stack nevertheless treats the two hands of a peer as two pointers, each with its own grab. The tap count is 1, 2, 3 and so on for taps that are close in time and space, and 0 for every other kind.

### Teardown and destroy order

The header documents no order of teardown. It gives these facts about the calls: `qar_library_destroy` frees the process-wide state of the library, and no other call may be used after it until the next `qar_library_init`; `qar_session_destroy` leaves and destroys a session; a subscription whose cancel token is NULL stays alive until the session is destroyed; `qar_gui_panels_close_panel` closes and removes a panel; and `qar_video_sender_handle_destroy` destroys a logical video sender and returns nothing.

This stack closes a GUI panel in three steps, in this order. First it cancels the input subscriptions of the panel, the gesture subscription and the panel-update subscription; the call does not return until no callback for the panel is running, so no callback can name a panel that is being destroyed, and the close that the stack itself makes is not mistaken for a close by the operator in AR. Second it calls `qar_gui_panels_close_panel`, unless the operator already closed the panel in AR, and it reports a failure of the call. Third it destroys the video sender with `qar_video_sender_handle_destroy`, whatever the second step did, because closing the sender first would leave a live panel that refers to a retired stream.

The operator can close a panel in AR. The state of a panel is reported by the subscription of `qar_gui_panels_subscribe_panel_updates`: the state `QAR_GUI_PANEL_STATE_CLOSED` means that the operator closed it, and the runtime republishes a closed panel freely, so this stack reports it once. The states minimised and hidden mean that the operator put the panel away, and the stream continues. When the operator closes a panel in AR, the Qt library stops capturing for the panel and does not touch its adapter or its sender; the teardown comes with the close of the Qt window or the shutdown of the session, and it tells the adapter that the operator already closed the panel.

A subscription is retired in a fixed order too. This stack cancels the token, so that the runtime stops delivering, then gives up the callback slot, so that a callback that is already running finds nothing to touch, and only then destroys the token handle.

QuaternAR states that the current runtime survives the destruction of a stream while other streams run. This stack therefore destroys the sender of a closed panel and forgets the stream.

The variable `QAR_PANEL_CLOSE_MODE` set to `notice` is the escape hatch for a runtime on which destroying a sender while other streams run is not safe: the stream and its sender are kept, and the AR panel shows a notice until the session ends.

At shutdown, each render stream first destroys a frame handle that was fetched and not used, and then the stream. The factory destroys the session, then the invite handle, then the runtime, and then, when the library is loaded, calls `qar_library_destroy`. The OpenGL contexts that this stack created for the senders are deleted only after the library is destroyed, so that no context is deleted while a thread of QarOS may still have it current, and the library is unloaded last.

### Known SDK limitations

These limits of the SDK are either observations of the maintainers from live sessions, which the SDK documentation does not describe and which have not been repeated on every drop, or places where the header leaves a question open:

- **No call retires a logical video stream.** `qar_video_sender_handle_destroy` returns nothing, and `qar_gui_panels_close_panel` closes the panel and not the stream behind it; the header declares no call that stops, retires or withdraws a stream. The maintainers observed that a receiver that is attached to the stream is not told that the stream is gone: it enters a reconnecting state and retries about every ten seconds, each retry fails with the status 507, and this goes on until its peer goes away. This stack destroys the sender and closes the panel when it retires a panel.
- **A cancelled gesture subscription may leave its runtime subscription.** Cancelling the token stops the delivery of events to the callback, but the header does not promise that a callback that is already running has returned when the cancel call returns, so this stack gives up the callback slot under a lock and never frees the slot. The maintainers also observed that the runtime-side subscription of a panel persisted until the session was destroyed, so a session that opens many panels keeps a pair of subscriptions for each of them.
- **A widget resize needs a new sender.** The receiver of a stream panel takes its resolution from the layout of the video sender, and the SDK does not document whether a change of that layout with `qar_video_sender_change_layout` reaches a receiver that is already attached. This stack therefore creates a new sender when the size of a widget changes.
- **What the runtime intersects a gesture with is not documented by the SDK.** The header names the action point as the intersection that the gesture acts on, and it does not say which geometry is intersected.
- **The viewport of URL panels is fixed.** The page of a URL panel sees a viewport of 500 by 500 CSS pixels at a device pixel ratio of 2.0, whatever the size of the panel, and the SDK does not document a way to configure it.
- **No call asks where the operator is.** The header declares the pose of the viewer, its head and its eyes, only with a render sender: `qar_render_frame_info_get_view_pose` on the frame information of a frame returns it in the app content space of the volume that the sender is bound to, and `qar_render_sender_last_hands` returns the hands of the same sender. A program that streams only panels has no render sender, so it cannot know where the operator stands or looks, and the code of this stack places its panels at fixed positions.
- **A panel that is created again after it was closed can crash the Visualizer.** With a Visualizer attached, the Visualizer crashed in each of the 25 cases that the maintainers observed in which the plugin of this stack, qar_rviz_plugin, created its hidden Displays panel, a GUI panel, again after it had been closed. The crash came within a second of the creation, every running Visualizer crashed with it, and QarOS started a Visualizer again after several seconds. It happened with the display name unchanged, with a numbered display name, with a shifted position and with a pause of 8 seconds before the start. In one longer session the Visualizer crashed five more times, each within a second of the close or the creation of a panel: twice at the close of the panel of a dialog, once at the close and the creation again of a video panel, and twice at the opening of the page of an AR tool in a URL panel. It did not crash at the other closes and creations of panels in that session. Every crash report names the same access violation at the same place inside `WebCore.dll`, the browser engine library of the Visualizer. The crash reports are files of the QarOS log folder that are named after the program `qar-stereokit-gui.exe`, which the maintainers call the Visualizer GUI process. The SDK `bin` folder holds this program next to `qar-streaming-viz.exe`, which this documentation names as the Visualizer elsewhere; how the two programs relate is not documented here. The SDK documents no rule for display names, and the defect is the Visualizer's. The plugin gives every QarOS panel that it starts a display name that no earlier panel of its process used: the name that was asked for at the first start, and `<name> (2)`, `<name> (3)` and so on at the later ones. This does not prevent the crash. It does not number the panel that the Qt library of this stack, qar_qt_streaming, creates for a dialog of a streamed window, the panel that this library creates again when the size of a streamed window changes, or the URL panels of the AR tools.
- **Further limits are named where they apply.** The OpenGL frame has no field for synchronisation, the SDK exposes no field for the role of a peer, and the encoding of the depth values is not documented.
