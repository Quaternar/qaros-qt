#pragma once

// Implementation of Subscription.hpp.

#include <type_traits>
#include <utility>

namespace qaros
{

namespace detail
{

struct ErasedState
{
	virtual ~ErasedState() = default;
};

template<typename Fn>
struct CallableState final : ErasedState
{
	explicit CallableState(Fn fn)
		: Callable(std::move(fn))
	{
	}

	Fn Callable;
};

/// The C function pointer of a subscription: forwards `Args...` to the
/// callable stored behind `user_state`. noexcept: an exception must not cross
/// back into the library's thread, it terminates instead.
template<typename State, typename... Args>
struct Forwarder
{
	static void Invoke(Args... args, void* user_state) noexcept
	{
		static_cast<State*>(user_state)->Callable(args...);
	}
};

/// Hands the user callable `Snapshot::FromCallback(raw)`, a value read while
/// the library's handle is valid.
template<typename Snapshot, typename Raw, typename Fn>
struct SnapshotAdapter
{
	Fn Callable;

	void operator()(Raw* raw) { Callable(Snapshot::FromCallback(raw)); }
};

} // namespace detail

inline Subscription::Subscription(
	CancelToken token, std::unique_ptr<detail::ErasedState> state
) noexcept
	: m_state(std::move(state))
	, m_token(std::move(token))
{
}

inline Subscription&
Subscription::operator=(Subscription&& other) noexcept
{
	if(this != &other)
	{
		End();
		m_state = std::move(other.m_state);
		m_token = std::move(other.m_token);
	}
	return *this;
}

inline Subscription::~Subscription() noexcept
{
	End();
}

inline Result<void>
Subscription::Cancel() const noexcept
{
	if(not m_token)
	{
		return Result<void>();
	}
	return m_token.Cancel();
}

inline void
Subscription::End() noexcept
{
	m_token.Reset();
	m_state.reset();
}

inline CancelTokenHandle*
Subscription::Token() const noexcept
{
	return m_token.Get();
}

template<typename... Args, typename Fn, typename Start>
Result<Subscription>
SubscribeWith(Fn&& fn, Start&& start) noexcept
{
	using State = detail::CallableState<std::decay_t<Fn>>;
	Result<CancelToken> token = CancelToken::Create();
	if(not token.Ok())
	{
		return Result<Subscription>(token.GetStatus());
	}
	auto state = std::make_unique<State>(std::forward<Fn>(fn));
	const Status subscribed(
		start(&detail::Forwarder<State, Args...>::Invoke, state.get(), token.Value().Get())
	);
	if(not subscribed.Ok())
	{
		return Result<Subscription>(subscribed);
	}
	return Subscription(std::move(token).Value(), std::move(state));
}

template<typename Snapshot, typename Raw, typename Fn, typename Start>
Result<Subscription>
SubscribeSnapshots(Fn&& fn, Start&& start) noexcept
{
	using Adapter = detail::SnapshotAdapter<Snapshot, Raw, std::decay_t<Fn>>;
	return SubscribeWith<Raw*>(Adapter{ std::forward<Fn>(fn) }, std::forward<Start>(start));
}

} // namespace qaros
