// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for the options of a stream, the capture, the injector and the pointer state machine. The
// functions are compiled and linked and never run; their parameters stand for what the host owns.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <QImage>
#include <QWidget>

#include <cstdio>
#include <memory>
#include <vector>

namespace qqs = qar_qt_streaming;

qqs::PanelStreamOptions snippet_stream_options()
{
  // [qar-snippet:stream_options]
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
  // [qar-snippet-end:stream_options]
  return options;
}

void snippet_capture(QWidget * widget, QWidget * camera_view, std::shared_ptr<qqs::LatestVideoFrame> camera)
{
  // [qar-snippet:capture]
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
  // [qar-snippet-end:capture]
}

void snippet_injector(QWidget * widget)
{
  // [qar-snippet:injector]
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
  // [qar-snippet-end:injector]
}

void snippet_pointer_state()
{
  // [qar-snippet:pointer_state]
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
  // [qar-snippet-end:pointer_state]
}
