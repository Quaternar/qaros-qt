#pragma once

// Turns a one-shot `qar_*_async` C call into a C++ completion (see Async.hpp
// for the tokens). Internal to this library.
//
// Relies on the C contract of every async call: when it returns an error the
// callback never fires, when it returns success the callback fires exactly
// once (also on cancellation), and progress never fires after the result.

#include <qar_streaming_cpp/Async.hpp>
#include <qar_streaming_cpp/CancelToken.hpp>

#include <functional>
#include <future>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace qaros
{

namespace detail
{

inline Progress
MakeProgress(QarActionSeverity severity, float percent, const char* message) noexcept
{
	const std::string_view text = message == nullptr ? std::string_view() : message;
	return Progress{ FromNative(severity), percent, text };
}

/// Progress of a blocking C call: `progress_state` is the ProgressHandler.
inline void
OnBlockingProgress(
	QarActionSeverity severity, float percent, const char* message, void* progress_state
) noexcept
{
	(*static_cast<const ProgressHandler*>(progress_state))(
		MakeProgress(severity, percent, message)
	);
}

inline qar_progress_callback_t
BlockingProgressCallback(const CallOptions& options) noexcept
{
	return options.OnProgress ? &OnBlockingProgress : nullptr;
}

inline void*
BlockingProgressState(const CallOptions& options) noexcept
{
	return options.OnProgress ? const_cast<ProgressHandler*>(&options.OnProgress) : nullptr;
}

/// What a C async call gets as `user_state`. Complete() consumes it.
/// `Context` keeps what the result refers to alive (e.g. the session of a
/// render sender being created).
template<typename V>
class Completion
{
public:
	Completion(ProgressHandler progress, std::shared_ptr<void> context)
		: m_progress(std::move(progress))
		, m_context(std::move(context))
	{
	}

	virtual ~Completion() = default;

	virtual void Complete(Result<V> result) noexcept = 0;

	void ReportProgress(const Progress& progress) const
	{
		if(m_progress)
		{
			m_progress(progress);
		}
	}

	template<typename T>
	[[nodiscard]]
	std::shared_ptr<T> Context() const noexcept
	{
		return std::static_pointer_cast<T>(m_context);
	}

private:
	ProgressHandler m_progress;
	std::shared_ptr<void> m_context;
};

template<typename V>
Completion<V>*
AsCompletion(void* user_state) noexcept
{
	return static_cast<Completion<V>*>(user_state);
}

template<typename V>
void
Finish(void* user_state, Result<V> result) noexcept
{
	AsCompletion<V>(user_state)->Complete(std::move(result));
}

template<typename V>
void
OnAsyncProgress(
	QarActionSeverity severity, float percent, const char* message, void* user_state
) noexcept
{
	AsCompletion<V>(user_state)->ReportProgress(MakeProgress(severity, percent, message));
}

/// The C progress callback of an async call completing with V, or none.
template<typename V>
qar_progress_callback_t
AsyncProgressCallback(const CallOptions& options) noexcept
{
	return options.OnProgress ? &OnAsyncProgress<V> : nullptr;
}

template<typename V, typename Handler>
class HandlerCompletion final : public Completion<V>
{
public:
	HandlerCompletion(Handler handler, ProgressHandler progress, std::shared_ptr<void> context)
		: Completion<V>(std::move(progress), std::move(context))
		, m_handler(std::move(handler))
	{
	}

	void Complete(Result<V> result) noexcept override
	{
		const std::unique_ptr<HandlerCompletion> self(this);
		m_handler(std::move(result));
	}

private:
	Handler m_handler;
};

/// `start(user_state, cancel)` makes the C call. A synchronous failure is
/// delivered to `handler` before this returns.
template<typename V, typename Start, typename Handler>
void
StartWithHandler(
	Start& start, CallOptions options, std::shared_ptr<void> context, Handler&& handler
) noexcept
{
	using State = HandlerCompletion<V, std::decay_t<Handler>>;
	auto state = std::make_unique<State>(
		std::forward<Handler>(handler), std::move(options.OnProgress), std::move(context)
	);
	const Status started(start(static_cast<Completion<V>*>(state.get()), options.Cancel));
	State* owned = state.release();
	if(not started.Ok())
	{
		owned->Complete(Result<V>(started));
	}
}

template<typename V>
struct PromiseSetter
{
	std::promise<Result<V>> Promise;
	void operator()(Result<V> result) { Promise.set_value(std::move(result)); }
};

template<typename V, typename Start>
std::future<Result<V>>
StartWithFuture(Start& start, CallOptions options, std::shared_ptr<void> context) noexcept
{
	PromiseSetter<V> setter;
	std::future<Result<V>> future = setter.Promise.get_future();
	StartWithHandler<V>(start, std::move(options), std::move(context), std::move(setter));
	return future;
}

#if QAR_STREAMING_CPP_HAS_ASIO

struct CancelOnSignal
{
	CancelTokenHandle* Token;

	void operator()(net::cancellation_type type) const
	{
		if((type & net::cancellation_type::all) == net::cancellation_type::none)
		{
			return;
		}
		Status(qar_cancel_token_cancel(Token)).LogIfError();
	}
};

template<typename V, typename Handler>
class AsioCompletion final : public Completion<V>
{
public:
	using Executor = net::associated_executor_t<Handler>;

	AsioCompletion(
		Handler handler,
		CancelToken own_token,
		ProgressHandler progress,
		std::shared_ptr<void> context
	)
		: Completion<V>(std::move(progress), std::move(context))
		, m_work(net::make_work_guard(net::get_associated_executor(handler)))
		, m_handler(std::move(handler))
		, m_ownToken(std::move(own_token))
	{
	}

	/// Asio forbids running the handler inside the initiating function.
	void MarkStartFailed() noexcept { m_startFailed = true; }

	void Complete(Result<V> result) noexcept override
	{
		Deliver deliver{ std::unique_ptr<AsioCompletion>(this), std::move(result) };
		Executor executor = m_work.get_executor();
		if(m_startFailed)
		{
			net::post(executor, std::move(deliver));
			return;
		}
		net::dispatch(executor, std::move(deliver));
	}

private:
	struct Deliver
	{
		std::unique_ptr<AsioCompletion> Self;
		Result<V> Value;

		void operator()() { Self->Finish(std::move(Value)); }
	};

	/// On the handler's executor, where the cancellation slot may be touched.
	void Finish(Result<V> result)
	{
		net::get_associated_cancellation_slot(m_handler).clear();
		m_ownToken.Reset();
		Handler handler = std::move(m_handler);
		m_work.reset();
		std::move(handler)(std::move(result));
	}

	net::executor_work_guard<Executor> m_work;
	Handler m_handler;
	CancelToken m_ownToken;
	bool m_startFailed = false;
};

template<typename V, typename Start>
struct AsioInitiation
{
	Start StartCall;
	CallOptions Options;
	std::shared_ptr<void> Context;

	template<typename Handler>
	void operator()(Handler&& handler)
	{
		using State = AsioCompletion<V, std::decay_t<Handler>>;
		auto slot = net::get_associated_cancellation_slot(handler);
		CancelTokenHandle* cancel = Options.Cancel;
		CancelToken own_token;
		Status token_status;
		// The caller's token may own other operations: an asio signal cancels a
		// token of this operation's own.
		if(cancel == nullptr && slot.is_connected())
		{
			Result<CancelToken> created = CancelToken::Create();
			token_status = created.GetStatus();
			own_token = std::move(created).Value();
			cancel = own_token.Get();
		}
		const bool cancel_on_signal = static_cast<bool>(own_token);
		auto state = std::make_unique<State>(
			std::forward<Handler>(handler),
			std::move(own_token),
			std::move(Options.OnProgress),
			std::move(Context)
		);
		if(not token_status.Ok())
		{
			state->MarkStartFailed();
			state.release()->Complete(Result<V>(token_status));
			return;
		}
		if(cancel_on_signal)
		{
			slot.template emplace<CancelOnSignal>(CancelOnSignal{ cancel });
		}
		const Status started(StartCall(static_cast<Completion<V>*>(state.get()), cancel));
		State* owned = state.release();
		if(not started.Ok())
		{
			owned->MarkStartFailed();
			owned->Complete(Result<V>(started));
		}
	}
};

#endif

/// Dispatches on the completion token. `start(Completion<V>*, CancelTokenHandle*)`
/// makes the C call with the given user_state and token.
template<typename V, typename Start, typename Token>
auto
Initiate(Start start, CallOptions options, std::shared_ptr<void> context, Token&& token) noexcept
{
	if constexpr(std::is_same_v<std::decay_t<Token>, UseFutureT>)
	{
		return StartWithFuture<V>(start, std::move(options), std::move(context));
	}
#if QAR_STREAMING_CPP_HAS_ASIO
	else
	{
		return net::async_initiate<Token, void(Result<V>)>(
			AsioInitiation<V, Start>{ std::move(start), std::move(options), std::move(context) },
			token
		);
	}
#else
	else
	{
		static_assert(
			std::is_invocable_v<std::decay_t<Token>&, Result<V>>,
			"completion must be qaros::UseFuture or callable with qaros::Result<V> "
			"(define QAR_STREAMING_CPP_WITH_ASIO for asio completion tokens)"
		);
		StartWithHandler<V>(
			start, std::move(options), std::move(context), std::forward<Token>(token)
		);
	}
#endif
}

} // namespace detail

} // namespace qaros
