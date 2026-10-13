// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for video in panels: the decoders, the mailbox, the video panel and the region sources. The
// functions are compiled and linked and never run; their parameters stand for what the host owns.

// The library headers come first: panel_pointer_state.hpp declares a method named emit(), which the emit macro of Qt breaks.
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/video_frame.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

#include <QImage>
#include <QSize>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace qqs = qar_qt_streaming;

void snippet_decode(
  const std::uint8_t * rgb, std::uint32_t width, std::uint32_t height, const std::uint8_t * jpeg, std::size_t jpeg_size,
  std::shared_ptr<qqs::LatestVideoFrame> mailbox)
{
  // [qar-snippet:decode]
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
  // [qar-snippet-end:decode]
}

void snippet_video_panel(
  qqs::QtPanelStreamManager * manager, qqs::VideoPanelWidget * view, std::shared_ptr<qqs::LatestVideoFrame> mailbox)
{
  // [qar-snippet:video_panel]
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
  // [qar-snippet-end:video_panel]
}

void snippet_region_source(
  qqs::QtPanelStreamManager * manager, QWidget * render_view, std::shared_ptr<qqs::LatestVideoFrame> mailbox)
{
  // [qar-snippet:region_source]
  // render_view is a child of the streamed widget that embeds a native window; QWidget::render() cannot paint it.
  const qar_ar_core::Result set = manager->set_panel_region_source("dock", render_view, mailbox);
  if (!set.ok()) {
    std::printf("no such panel: %s\n", set.message.c_str());   // NotFound for a key that does not stream
  }
  manager->clear_panel_region_sources("dock");           // stop painting video over every region of the panel
  // [qar-snippet-end:region_source]
}
