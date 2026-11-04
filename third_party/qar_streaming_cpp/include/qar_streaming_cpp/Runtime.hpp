#pragma once

// C modules: detail/runtime.h, the runtime calls of detail/onboarding.h.
// Async methods: see Async.hpp.

#include <qar_streaming_cpp/Async.hpp>
#include <qar_streaming_cpp/RuntimeBuilders.hpp>
#include <qar_streaming_cpp/Session.hpp>

namespace qaros
{

struct OnboardSession
{
	/// Persist it: the ticket for every later rejoin and forget.
	OnboardingId OnboardingId = {};
	qaros::Session Session;
};

/// A runtime hosting sessions. Releasing it leaves the runtime running.
class Runtime
{
public:
	Runtime() noexcept = default;
	explicit Runtime(RuntimePtr owned) noexcept;

	static Result<Runtime> Create(const RuntimeBuilder& builder = RuntimeBuilder()) noexcept;

	[[nodiscard]]
	RuntimeHandle* Get() const noexcept;
	explicit operator bool() const noexcept;
	/// Gives up ownership without releasing, for code that owns it in C.
	[[nodiscard]]
	RuntimeHandle* Release() noexcept;

	/// Full onboarding; the builder names the mode (code or invite).
	[[nodiscard]]
	Result<OnboardSession>
	Onboard(const OnboardBuilder& builder, const CallOptions& options = {}) const noexcept;
	/// Completes with Result<OnboardSession>.
	template<typename Token = UseFutureT>
	auto OnboardAsync(
		const OnboardBuilder& builder, CallOptions options = {}, Token&& token = Token{}
	) const noexcept;

	/// Restores a persisted onboarding.
	[[nodiscard]]
	Result<Session>
	Rejoin(const RejoinBuilder& builder, const CallOptions& options = {}) const noexcept;
	/// Completes with Result<Session>.
	template<typename Token = UseFutureT>
	auto RejoinAsync(
		const RejoinBuilder& builder, CallOptions options = {}, Token&& token = Token{}
	) const noexcept;

	/// Erases a persisted identity slot. Its session must be gone.
	[[nodiscard]]
	Result<void> Forget(const OnboardingId& onboarding_id) const noexcept;

private:
	RuntimePtr m_handle;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/Runtime.inl>
