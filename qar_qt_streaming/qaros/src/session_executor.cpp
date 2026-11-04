// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "backend_detail.hpp"

#include <utility>

namespace qar_qt_streaming::qaros_backend::detail
{

SessionExecutor::SessionExecutor(
  qaros::Session session, Post post, std::chrono::milliseconds budget)
: state_(std::make_shared<State>()),
  post_(std::move(post)),
  budget_(budget)
{
  state_->session = std::move(session);
  if (!post_) {
    const std::shared_ptr<State> state = state_;
    worker_ = std::thread([state] { run(state); });
  }
}

SessionExecutor::~SessionExecutor()
{
  shutdown();
}

void
SessionExecutor::run(const std::shared_ptr<State> & state)
{
  for (;;) {
    Item item;
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->wake.wait(lock, [&state] { return !state->queue.empty() || state->stopping; });
      if (state->queue.empty()) {
        break;  // stopping, and every queued task has run
      }
      item = std::move(state->queue.front());
      state->queue.pop_front();
    }
    try {
      item.task(state->session);
    } catch (...) {
      // A task that throws must not end the worker: the tasks after it (the teardowns) still
      // have to run.
    }
  }
  // The worker's copy of the session goes here, on the worker, and not on the thread that
  // destroys the executor (the Qt thread): letting go of the last reference may wait.
  state->session = qaros::Session();
  {
    const std::lock_guard<std::mutex> lock(state->mutex);
    state->done = true;
  }
  state->finished.notify_all();
}

bool
SessionExecutor::post(const std::string & name, Task task)
{
  const std::shared_ptr<State> state = state_;
  if (post_) {
    {
      const std::lock_guard<std::mutex> lock(state->mutex);
      if (state->stopping) {
        return false;
      }
    }
    return post_(
      name.c_str(), [state, task = std::move(task)]() mutable { task(state->session); });
  }
  {
    const std::lock_guard<std::mutex> lock(state->mutex);
    if (state->stopping) {
      return false;
    }
    state->queue.push_back(Item{name, std::move(task)});
  }
  state->wake.notify_one();
  return true;
}

bool
SessionExecutor::accepting() const
{
  const std::lock_guard<std::mutex> lock(state_->mutex);
  return !state_->stopping;
}

void
SessionExecutor::shutdown()
{
  if (shut_down_.exchange(true)) {
    return;
  }
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->stopping = true;
  if (post_) {
    return;  // the application's executor owns its threads; nothing to join
  }
  state_->wake.notify_all();
  const bool finished = state_->finished.wait_for(lock, budget_, [this] { return state_->done; });
  lock.unlock();
  if (finished) {
    worker_.join();
  } else {
    worker_.detach();
    abandoned_.store(true);
  }
}

bool
SessionExecutor::abandoned() const
{
  return abandoned_.load();
}

}  // namespace qar_qt_streaming::qaros_backend::detail
