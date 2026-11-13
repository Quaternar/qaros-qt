#pragma once

#include <qar_streaming_cpp/CancelToken.hpp>

#include <memory>

namespace qaros
{

namespace detail
{
struct ErasedState;
} // namespace detail

/// A subscription's lifetime: its cancel token and the callable behind its
/// `user_state`. Ending it destroys the token first, which waits for a running
/// callback, then frees the callable.
///
/// From inside the subscription's own callback call Cancel(), never End() or
/// the destructor: the callable is still running there.
class Subscription
{
public:
	Subscription() noexcept = default;
	Subscription(CancelToken token, std::unique_ptr<detail::ErasedState> state) noexcept;
	Subscription(Subscription&& other) noexcept = default;
	Subscription& operator=(Subscription&& other) noexcept;
	Subscription(const Subscription&) = delete;
	Subscription& operator=(const Subscription&) = delete;
	~Subscription() noexcept;

	/// Stops callbacks; safe from inside one. Keeps the callable alive.
	[[nodiscard]]
	Result<void> Cancel() const noexcept;
	/// Cancels, waits for a running callback, frees the callable.
	void End() noexcept;
	[[nodiscard]]
	CancelTokenHandle* Token() const noexcept;

private:
	// Destroyed after m_token on every path, End() included.
	std::unique_ptr<detail::ErasedState> m_state;
	CancelToken m_token;
};

/// Starts any `qar_*_subscribe_*` call. `Args` are the C callback's arguments
/// before `user_state`; `start(c_callback, user_state, token)` makes the C call.
///
/// `fn` runs on a library thread; synchronise what it touches.
template<typename... Args, typename Fn, typename Start>
Result<Subscription> SubscribeWith(Fn&& fn, Start&& start) noexcept;

/// A subscription whose callback carries one handle `Raw*`: `fn` receives
/// `Result<Snapshot>`, read from it with `Snapshot::FromCallback`.
template<typename Snapshot, typename Raw, typename Fn, typename Start>
Result<Subscription> SubscribeSnapshots(Fn&& fn, Start&& start) noexcept;

} // namespace qaros

#include <qar_streaming_cpp/detail/Subscription.inl>
