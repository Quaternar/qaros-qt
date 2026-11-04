// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_HPP_
#define QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_HPP_

/// @file qaros_panel_sender.hpp
/// @brief One AR panel over the QarOS C++ SDK: a CPU video sender and the GUI panel that shows its
/// stream, behind the panel seam of the library (seam::IPanelSender).
///
/// ## Threads
///
/// Everything that can wait on the runtime runs on the factory's worker, one task after the other
/// in the order they were posted: making the video sender and the panel, probing and reattaching a
/// panel of an earlier session, the subscriptions, the placement, the state and the teardown.
/// The Qt thread makes only the three calls of the frame cycle (`TryBeginFrame`, `GetCpuFrame`,
/// `ShowFrame`), and only while the panel is made and not yet closed. The outcomes of the worker's
/// tasks come back through `drain_results`, which the manager calls on every tick.
///
/// ## Close order
///
/// The gesture subscription ends first, then the panel-updates subscription, then the panel is
/// closed (unless the operator closed it in AR, or the sender is only detached), then the video
/// sender is released. The runtime republishes a panel that is closed as CLOSED; with the
/// subscription still live that echo would come back as "the operator closed the panel", and a
/// late gesture could reach a panel that is being retired.

#include "qar_qt_streaming/qaros/qaros_panel_input_source.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"
#include "qar_qt_streaming/seam/panel_sender.hpp"

#include <qar_streaming_cpp/VideoSender.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qar_qt_streaming::qaros_backend
{

namespace detail
{
class DiagnosticsHub;
class SessionExecutor;
}  // namespace detail

class QarosPanelSender final : public seam::IPanelSender
{
public:
  using Clock = std::chrono::steady_clock;

  /// What the factory shares with every sender it makes.
  struct Context
  {
    std::shared_ptr<detail::SessionExecutor> executor;
    std::shared_ptr<detail::DiagnosticsHub> hub;
    /// Null for a panel that streams and takes no input.
    std::shared_ptr<QarosPanelInputSource> input;
    /// The clock of the panel-state guard. Unset: the steady clock.
    std::function<Clock::time_point()> clock;
  };

  /// Made by QarosPanelSenderFactory. Posts the creation to the worker and returns at once; the
  /// sender reports "creating" until the panel exists.
  QarosPanelSender(Context context, const seam::PanelDesc & desc);
  ~QarosPanelSender() override;

  QarosPanelSender(const QarosPanelSender &) = delete;
  QarosPanelSender & operator=(const QarosPanelSender &) = delete;

  /// True when the sender was refused at construction (see last_error()).
  bool refused() const { return refused_; }

  /// True once the video sender and the panel are made.
  bool ready() const;

  bool pending() const override;

  /// The common name the panel was made under (a suffix is appended when the name was taken by a
  /// panel that cannot be reused). Empty until the panel is made.
  std::string common_name() const;

  /// True when the panel was one of an earlier session that was found again.
  bool reattached() const;

  bool try_begin_frame(bool & out_ready) override;
  seam::PanelNotReadyCause not_ready_cause() const override { return not_ready_cause_; }
  bool submit_frame(const seam::PanelPixels & pixels) override;

  bool set_placement(const seam::PanelPlacement & placement) override;
  bool set_state(seam::PanelState state) override;
  bool supports_state() const override { return true; }

  using seam::IPanelSender::close;
  seam::PanelCloseReport close(bool panel_closed_in_ar) override;
  void detach() override;
  bool supports_detach() const override { return true; }

  const std::string & last_error() const override { return last_error_; }
  std::string stream_id_text() const override;
  void drain_results(std::vector<seam::PanelOpResult> & out) override;

private:
  struct Core;

  seam::PanelCloseReport teardown(bool panel_closed_in_ar, bool keep_panel);
  void refuse(const std::string & reason);

  Context context_;
  seam::PanelDesc desc_;
  std::shared_ptr<Core> core_;

  // The members below belong to the Qt thread.
  qaros::VideoSender * sender_ = nullptr;
  bool refused_ = false;
  bool closed_ = false;
  bool frame_reserved_ = false;
  std::string last_error_;
  seam::PanelNotReadyCause not_ready_cause_ = seam::PanelNotReadyCause::NoDestination;
  std::uint64_t frames_shown_ = 0;
};

}  // namespace qar_qt_streaming::qaros_backend

#endif  // QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_HPP_
