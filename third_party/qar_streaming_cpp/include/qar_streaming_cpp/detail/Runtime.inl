#pragma once

// Implementation of Runtime.hpp.

#include <qar_streaming_cpp/detail/Completion.hpp>
#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

namespace detail
{

inline void
OnOnboarded(
	NativeResult status,
	const native::OnboardingId* onboarding_id,
	SessionHandle* session,
	void* user_state
) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<OnboardSession>(user_state, Result<OnboardSession>(result));
		return;
	}
	OnboardSession value;
	value.OnboardingId = FromNative(*onboarding_id);
	value.Session = Session(SessionPtr(session));
	Finish<OnboardSession>(user_state, std::move(value));
}

inline void
OnRejoined(
	NativeResult status,
	[[maybe_unused]] const native::OnboardingId* echoed_id,
	SessionHandle* session,
	void* user_state
) noexcept
{
	const Status result(status);
	if(not result.Ok())
	{
		Finish<Session>(user_state, Result<Session>(result));
		return;
	}
	Finish<Session>(user_state, Session(SessionPtr(session)));
}

} // namespace detail

inline Runtime::Runtime(RuntimePtr owned) noexcept
	: m_handle(std::move(owned))
{
}

inline Result<Runtime>
Runtime::Create(const RuntimeBuilder& builder) noexcept
{
	RuntimePtr runtime;
	const Status status(qar_runtime_create(&builder.Get(), OutPtr(runtime)));
	if(not status.Ok())
	{
		return Result<Runtime>(status);
	}
	return Runtime(std::move(runtime));
}

inline RuntimeHandle*
Runtime::Get() const noexcept
{
	return m_handle.get();
}

inline Runtime::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline RuntimeHandle*
Runtime::Release() noexcept
{
	return m_handle.release();
}

inline Result<OnboardSession>
Runtime::Onboard(const OnboardBuilder& builder, const CallOptions& options) const noexcept
{
	OnboardSession value;
	SessionPtr session;
	const Status status(qar_runtime_onboard(
		m_handle.get(),
		&builder.Get(),
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel,
		detail::OutValue(&value.OnboardingId),
		OutPtr(session)
	));
	if(not status.Ok())
	{
		return Result<OnboardSession>(status);
	}
	value.Session = Session(std::move(session));
	return value;
}

template<typename Token>
auto
Runtime::OnboardAsync(
	const OnboardBuilder& builder, CallOptions options, Token&& token
) const noexcept
{
	using V = OnboardSession;
	const qar_progress_callback_t progress = detail::AsyncProgressCallback<V>(options);
	const auto start = [runtime = m_handle.get(),
						init = &builder.Get(),
						progress](void* user_state, CancelTokenHandle* cancel)
	{
		return qar_runtime_onboard_async(
			runtime, init, &detail::OnOnboarded, progress, user_state, cancel
		);
	};
	return detail::Initiate<V>(start, std::move(options), nullptr, std::forward<Token>(token));
}

inline Result<Session>
Runtime::Rejoin(const RejoinBuilder& builder, const CallOptions& options) const noexcept
{
	SessionPtr session;
	const Status status(qar_runtime_rejoin(
		m_handle.get(),
		&builder.Get(),
		detail::BlockingProgressCallback(options),
		detail::BlockingProgressState(options),
		options.Cancel,
		OutPtr(session)
	));
	if(not status.Ok())
	{
		return Result<Session>(status);
	}
	return Session(std::move(session));
}

template<typename Token>
auto
Runtime::RejoinAsync(
	const RejoinBuilder& builder, CallOptions options, Token&& token
) const noexcept
{
	using V = Session;
	const qar_progress_callback_t progress = detail::AsyncProgressCallback<V>(options);
	const auto start = [runtime = m_handle.get(),
						init = &builder.Get(),
						progress](void* user_state, CancelTokenHandle* cancel)
	{
		return qar_runtime_rejoin_async(
			runtime, init, &detail::OnRejoined, progress, user_state, cancel
		);
	};
	return detail::Initiate<V>(start, std::move(options), nullptr, std::forward<Token>(token));
}

inline Result<void>
Runtime::Forget(const OnboardingId& onboarding_id) const noexcept
{
	native::ForgetInit init = native::Default<native::ForgetInit>();
	init.onboarding_id = ToNative(onboarding_id);
	return detail::ToVoid(qar_runtime_forget(m_handle.get(), &init));
}

} // namespace qaros
