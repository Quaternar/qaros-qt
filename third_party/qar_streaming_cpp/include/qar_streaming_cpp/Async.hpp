#pragma once

// What every async method takes: a completion token last, and CallOptions.
//
// - qaros::UseFuture (the default) -> std::future<qaros::Result<V>>
// - any callable f(Result<V>)      -> called on the library thread
// - with asio enabled, any asio completion token (use_awaitable, deferred,
//   use_future, bind_executor(...), a plain handler, ...). The handler runs on
//   its associated executor, and an asio cancellation signal cancels the call.
//
// Init structs are read when the C call starts: with asio::deferred keep them
// alive until the operation is started.

#include <qar_streaming_cpp/Status.hpp>

#include <functional>
#include <string_view>

namespace qaros
{

struct UseFutureT
{
	explicit UseFutureT() = default;
};

inline constexpr UseFutureT UseFuture{};

struct Progress
{
	ActionSeverity Severity;
	float Percent;
	/// Valid only during the progress call.
	std::string_view Message;
};

/// Runs on the library thread (async) or the calling thread (blocking), never
/// after the result.
using ProgressHandler = std::function<void(const Progress&)>;

/// Optional inputs of a call that can be cancelled or reports progress.
struct CallOptions
{
	/// Not owned. Cancelling it fails the call with a cancellation status.
	CancelTokenHandle* Cancel = nullptr;
	/// Ignored by calls without progress.
	ProgressHandler OnProgress = {};
};

} // namespace qaros
