// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_FACTORY_HPP_
#define QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_FACTORY_HPP_

/// @file qaros_panel_sender_factory.hpp
/// @brief The factory that makes one seam::IPanelSender per panel over a QarOS session of the C++
/// SDK. It is what a Qt application hands to the library's configuration.
///
/// The application owns the session and passes a copy. The factory runs every call that can wait
/// on the runtime on one worker thread (or on the application's own executor); the Qt thread is
/// never asked to wait. At the end of the program reset the panel manager first (it closes the
/// panels), then the factory (it runs the queued closes and joins its worker within the budget),
/// then release the application's copy of the session, never inside a callback of the SDK.

#include "qar_qt_streaming/qaros/qaros_panel_input_source.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"
#include "qar_qt_streaming/seam/panel_sender.hpp"
#include "qar_qt_streaming/seam/panel_types.hpp"

#include <qar_streaming_cpp/Session.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace qar_qt_streaming::qaros_backend
{

namespace detail
{
class DiagnosticsHub;
class SessionExecutor;
}  // namespace detail

struct FactoryOptions
{
  /// Optional: the application's own executor for the tasks that wait on the runtime. It must
  /// run them one after the other in the order they were posted, off the Qt thread, and return
  /// false when it takes no more. Unset: the factory starts a worker thread of its own.
  std::function<bool(const char * name, std::function<void()> task)> post;

  /// How long the factory's destructor waits for its worker to run the queued tasks. The wait is
  /// the one block on the calling thread, at the end of the program only. A worker that is still
  /// busy after it is abandoned; it ends by itself when its task returns.
  std::chrono::milliseconds shutdown_budget{8000};

  /// Where the messages of the backend go. Called on the Qt thread only: from the drain calls of
  /// the senders and of the input source, and from the factory's destructor. It must outlive the
  /// factory. Unset: the messages are dropped.
  seam::PanelDiagnosticsSink * diagnostics = nullptr;

  /// The clock of the panel-state guard. Unset: the steady clock. A test replaces it.
  std::function<std::chrono::steady_clock::time_point()> clock;
};

class QarosPanelSenderFactory final : public seam::IPanelSenderFactory
{
public:
  /// @param session A joined session; the factory keeps a copy until it is destroyed.
  /// @param input The input source the senders subscribe their panels with. Null: the panels
  ///        stream and take no input.
  QarosPanelSenderFactory(
    qaros::Session session, std::shared_ptr<QarosPanelInputSource> input = nullptr,
    FactoryOptions options = {});
  ~QarosPanelSenderFactory() override;

  QarosPanelSenderFactory(const QarosPanelSenderFactory &) = delete;
  QarosPanelSenderFactory & operator=(const QarosPanelSenderFactory &) = delete;

  /// Returns at once. The panel and its video sender are made on the worker; the sender it
  /// returns reports "creating" until then. Null when the description is refused (see
  /// last_error()).
  std::shared_ptr<seam::IPanelSender> create_panel_sender(const seam::PanelDesc & desc) override;

  const std::string & last_error() const override { return last_error_; }

  const std::shared_ptr<QarosPanelInputSource> & input() const { return input_; }

  /// True when the destructor's wait ran out and the worker was abandoned.
  bool worker_abandoned() const;

private:
  std::shared_ptr<detail::SessionExecutor> executor_;
  std::shared_ptr<detail::DiagnosticsHub> hub_;
  std::shared_ptr<QarosPanelInputSource> input_;
  std::function<std::chrono::steady_clock::time_point()> clock_;
  bool has_session_ = false;
  std::string last_error_;
};

}  // namespace qar_qt_streaming::qaros_backend

#endif  // QAR_QT_STREAMING__QAROS__QAROS_PANEL_SENDER_FACTORY_HPP_
