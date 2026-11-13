// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Snippet of API.md for the sender seam of the panel path. The function is compiled and linked and never run.
// Its parameters stand for what the host owns, so each one is a pointer or a reference that the documentation
// names in its text.

#include "qar_qt_streaming/seam/common_name.hpp"
#include "qar_qt_streaming/seam/panel_sender.hpp"
#include "qar_qt_streaming/seam/panel_types.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

namespace seam = qar_qt_streaming::seam;

void snippet_panel_seam(seam::IPanelSenderFactory * factory, const seam::NameScope & scope, const std::uint8_t * pixels)
{
  // [qar-snippet:panel_seam]
  seam::PanelDesc desc;
  desc.key = "status";                                    // the key that the host uses for the panel
  desc.display_name = "Status";                           // the title that QarOS shows
  desc.common_name = seam::make_panel_common_name(desc.key, scope);   // the name that finds the panel again
  desc.buffer_width = 1024;                               // the size of the buffer is fixed for the sender
  desc.buffer_height = 768;
  desc.placement.pose.position = seam::Vector3{0.0f, 1.6f, -1.0f};
  desc.placement.size = seam::PanelSize{0.8f, 0.6f, 1.0f};

  const std::shared_ptr<seam::IPanelSender> sender = factory->create_panel_sender(desc);
  if (!sender) {
    std::fprintf(stderr, "panel: %s\n", factory->last_error().c_str());
    return;
  }

  // Once for each tick, on the thread of the host that owns the widget.
  bool ready = false;
  if (sender->try_begin_frame(ready) && ready) {
    seam::PanelPixels frame;
    frame.data = pixels;
    frame.width = desc.buffer_width;
    frame.height = desc.buffer_height;
    frame.stride_bytes = desc.buffer_width * 4u;          // RGBA: four bytes for each pixel
    sender->submit_frame(frame);                          // copies the pixels; the buffer stays the host's
  }

  sender->close(false);                                   // the one teardown, in a fixed order
  // [qar-snippet-end:panel_seam]
}
