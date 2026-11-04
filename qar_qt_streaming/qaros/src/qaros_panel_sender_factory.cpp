// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/qaros/qaros_panel_sender_factory.hpp"

#include "backend_detail.hpp"
#include "qar_qt_streaming/qaros/qaros_panel_sender.hpp"
#include "qar_qt_streaming/qaros/sdk_config_check.hpp"

#include <utility>

namespace qar_qt_streaming::qaros_backend
{

QarosPanelSenderFactory::QarosPanelSenderFactory(
  qaros::Session session, std::shared_ptr<QarosPanelInputSource> input, FactoryOptions options)
: hub_(std::make_shared<detail::DiagnosticsHub>(options.diagnostics)),
  input_(std::move(input)),
  clock_(std::move(options.clock)),
  has_session_(static_cast<bool>(session))
{
  if (!has_session_) {
    last_error_ = "no QarOS session";
  }
  // The executor takes the factory's copy of the session: the tasks use it, and it is released on
  // the worker.
  executor_ = std::make_shared<detail::SessionExecutor>(
    std::move(session), std::move(options.post), options.shutdown_budget);
  if (input_ && options.diagnostics != nullptr) {
    input_->set_diagnostics(options.diagnostics);
    const std::shared_ptr<detail::DiagnosticsHub> hub = hub_;
    input_->set_message_flush([hub] {hub->flush();});
  }
}

QarosPanelSenderFactory::~QarosPanelSenderFactory()
{
  // The queued tasks (the closes of the senders that were closed before) run, then the worker is
  // joined within the budget. Senders that outlive the factory can post no more.
  executor_->shutdown();
  // The closes that ran during the wait left their messages; the sink gets them here, on the
  // thread that destroys the factory (the Qt thread).
  hub_->flush();
}

bool
QarosPanelSenderFactory::worker_abandoned() const
{
  return executor_->abandoned();
}

std::shared_ptr<seam::IPanelSender>
QarosPanelSenderFactory::create_panel_sender(const seam::PanelDesc & desc)
{
  if (!has_session_) {
    last_error_ = "no QarOS session";
    return nullptr;
  }
  if (!executor_->accepting()) {
    last_error_ = "QarOS session is not accepting calls";
    return nullptr;
  }
  QarosPanelSender::Context context;
  context.executor = executor_;
  context.hub = hub_;
  context.input = input_;
  context.clock = clock_;

  // The panel and its video sender are made on the worker; the sender is handed back at once,
  // still "creating".
  auto sender = std::make_shared<QarosPanelSender>(std::move(context), desc);
  if (sender->refused()) {
    last_error_ = sender->last_error();
    return nullptr;
  }
  last_error_.clear();
  return sender;
}

}  // namespace qar_qt_streaming::qaros_backend
