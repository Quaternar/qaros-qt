#pragma once

// C module: detail/onboarding.h (invite handles)

#include <qar_streaming_cpp/Handle.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace qaros
{

/// An onboarding invite: minted by a hub, serialized to hand to a sibling
/// instance, or onboarded with through OnboardBuilder::WithInvite.
class OnboardingInvite
{
public:
	OnboardingInvite() noexcept = default;
	explicit OnboardingInvite(OnboardingInvitePtr owned) noexcept;

	static Result<OnboardingInvite> Deserialize(const uint8_t* data, size_t size) noexcept;
	static Result<OnboardingInvite> Deserialize(const std::vector<uint8_t>& wire) noexcept;

	[[nodiscard]]
	OnboardingInviteHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	Result<std::vector<uint8_t>> Serialize() const noexcept;
	[[nodiscard]]
	Result<OnboardingMethod> GetMethod() const noexcept;
	[[nodiscard]]
	Result<int64_t> GetExpiresUnix() const noexcept;
	/// The pairing code of a PAKE invite. A secret: never log it.
	[[nodiscard]]
	Result<std::string> GetCode() const noexcept;

private:
	OnboardingInvitePtr m_handle;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/OnboardingInvite.inl>
