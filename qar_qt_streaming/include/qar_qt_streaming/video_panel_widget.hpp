// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__VIDEO_PANEL_WIDGET_HPP_
#define QAR_QT_STREAMING__VIDEO_PANEL_WIDGET_HPP_

/// @file video_panel_widget.hpp
/// @brief The content of a stand-alone AR video panel (variant a): a host opens
/// one of these as a NEW panel for a video stream, fed by a `LatestVideoFrame` a
/// worker thread publishes into.
///
/// Built exactly like panel_tombstone.hpp's placeholder: parentless, carrying
/// `Qt::WA_DontShowOnScreen` and already `show()`n, so `QWidget::render()`
/// (which requires its children — and, transitively, itself — to be
/// *logically* visible) can capture it with no window ever mapped onto the
/// operator's desktop. See panel_tombstone.cpp's header comment for why an
/// owned QWidget is used rather than a hand-rolled drawing routine fed
/// straight into a buffer: it reuses `QtWidgetCapture`'s geometry, DPR and
/// dirty-check handling unchanged.

#include "qar_qt_streaming/latest_video_frame.hpp"

#include <QString>
#include <QWidget>

#include <cstdint>
#include <memory>

class QPaintEvent;

namespace qar_qt_streaming
{

/// @brief One stand-alone AR video panel's content: paints the latest frame
/// from a `LatestVideoFrame`, aspect-fit on black, with a status message when
/// there is none yet and a slim caption strip.
class VideoPanelWidget : public QWidget
{
public:
  /// @param source Shared with whatever decodes the video (never null in
  ///        practice, but a null source simply always shows the default
  ///        "waiting" text — see @ref paintEvent).
  /// @param title Shown in the caption strip. May be empty.
  /// @param logical_width Initial width in Qt logical pixels; height follows a
  ///        16:9 guess (`logical_width * 9 / 16`) until the first frame's own
  ///        aspect ratio arrives and @ref sync_to_frame corrects it.
  explicit VideoPanelWidget(
    std::shared_ptr<LatestVideoFrame> source,
    QString title = QString(),
    int logical_width = 640);

  /// @brief Adopt the newest frame's aspect ratio if it has drifted from the
  /// widget's own by more than 1%: resizes the HEIGHT to match (width is kept,
  /// since that is what the panel's placement was chosen around), and returns
  /// true when it did.
  ///
  /// Called by the owner on the GUI thread once per tick, deliberately NOT
  /// from @ref paintEvent — resizing from inside a paint event is fragile (Qt
  /// may deliver the resize's own repaint before this one has finished), and
  /// the caller's tick is exactly the place a resize is already expected: see
  /// `QtPanelStream::tick` / `refresh_geometry`, which reacts to the widget's
  /// new size through the ordinary resize-hysteresis path — this class does
  /// not, and must not, talk to `QtPanelStream` directly.
  bool sync_to_frame();

  /// @brief The `LatestVideoFrame::sequence()` observed at the most recent
  /// @ref paintEvent. For tests: confirms a capture actually painted the frame
  /// it was meant to, rather than a stale one.
  std::uint64_t painted_sequence() const { return painted_sequence_; }

  /// @brief The mailbox this panel paints from (may be null). QtPanelStream
  /// watches its sequence so the panel is captured when a frame ARRIVES rather
  /// than on a timer-quantised schedule — see capture_gate.hpp.
  const std::shared_ptr<LatestVideoFrame> & source() const { return source_; }

protected:
  void paintEvent(QPaintEvent * event) override;

private:
  std::shared_ptr<LatestVideoFrame> source_;
  QString title_;
  std::uint64_t painted_sequence_ = 0;
};

}  // namespace qar_qt_streaming

#endif  // QAR_QT_STREAMING__VIDEO_PANEL_WIDGET_HPP_
