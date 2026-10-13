// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/video_panel_widget.hpp"
#include "qar_qt_streaming/video_frame.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <utility>

namespace qar_qt_streaming
{

VideoPanelWidget::VideoPanelWidget(
  std::shared_ptr<LatestVideoFrame> source, QString title, int logical_width)
: source_(std::move(source)),
  title_(std::move(title))
{
  // Parentless + WA_DontShowOnScreen + show(): see the file comment. This
  // makes the widget (and, once laid out, its children) `isVisible()`, which
  // `QWidget::render()` requires, without a window ever appearing on the
  // operator's desktop.
  setAttribute(Qt::WA_DontShowOnScreen, true);
  setObjectName(QStringLiteral("qarVideoPanel"));

  const int initial_width = logical_width > 0 ? logical_width : 1;
  const int initial_height = std::max(1, initial_width * 9 / 16);
  resize(initial_width, initial_height);
  show();
  ensurePolished();
}

bool
VideoPanelWidget::sync_to_frame()
{
  if (!source_ || width() <= 0 || height() <= 0) {
    return false;
  }
  const std::shared_ptr<const QImage> frame = source_->latest();
  if (!frame || frame->isNull() || frame->width() <= 0 || frame->height() <= 0) {
    return false;
  }

  const double current_aspect = static_cast<double>(width()) / static_cast<double>(height());
  const double frame_aspect =
    static_cast<double>(frame->width()) / static_cast<double>(frame->height());
  const double relative_diff = std::fabs(frame_aspect - current_aspect) / current_aspect;
  if (relative_diff <= 0.01) {
    return false;
  }

  const int new_height = std::max(1, static_cast<int>(std::lround(width() / frame_aspect)));
  resize(width(), new_height);
  return true;
}

void
VideoPanelWidget::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);

  std::shared_ptr<const QImage> frame;
  std::string status_text;
  double fps = 0.0;
  if (source_) {
    frame = source_->latest();
    status_text = source_->status();
    fps = source_->fps();
    painted_sequence_ = source_->sequence();
  }

  if (frame && !frame->isNull()) {
    const QRect dest = aspect_fit_rect(frame->size(), rect());
    painter.drawImage(dest, *frame);
  } else {
    // A dark GREY fill, not the letterbox black above, so "waiting" reads as
    // a distinct state from "video playing with letterbox bars" even where a
    // platform rasterizes no glyphs for the status text below (the offscreen
    // QPA plugin this suite runs under, notably) -- the fill alone is enough
    // for a capture of this state to not be uniformly black.
    painter.fillRect(rect(), QColor(0x22, 0x22, 0x22));
    painter.setPen(QColor(0xC0, 0xC0, 0xC0));
    const QString message = status_text.empty()
      ? QStringLiteral("Waiting for video...")
      : QString::fromStdString(status_text);
    painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, message);
  }

  // A slim, legible caption strip along the bottom -- kept small on purpose,
  // it labels the picture for the operator rather than competing with it.
  const int caption_height = std::max(12, height() / 12);
  const QRect caption_rect(0, height() - caption_height, width(), caption_height);
  painter.fillRect(caption_rect, QColor(0, 0, 0, 170));

  QString caption = title_;
  if (frame && !frame->isNull()) {
    if (!caption.isEmpty()) {
      caption += QChar(' ');
    }
    caption += QString::fromLatin1("%1x%2 ").arg(frame->width()).arg(frame->height());
    caption += QChar(0x00B7);  // '.' would read as a typo; this is the actual separator
    caption += QString::fromLatin1(" %1 fps").arg(fps, 0, 'f', 1);
  }
  if (!caption.isEmpty()) {
    QFont font = painter.font();
    font.setPixelSize(std::max(8, caption_height - 4));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(caption_rect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, caption);
  }
}

}  // namespace qar_qt_streaming
