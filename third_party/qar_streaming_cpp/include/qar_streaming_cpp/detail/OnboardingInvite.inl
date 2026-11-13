#pragma once

// Implementation of OnboardingInvite.hpp.

#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

inline OnboardingInvite::OnboardingInvite(OnboardingInvitePtr owned) noexcept
	: m_handle(std::move(owned))
{
}

inline Result<OnboardingInvite>
OnboardingInvite::Deserialize(const uint8_t* data, size_t size) noexcept
{
	OnboardingInvitePtr invite;
	const Status status(qar_onboarding_invite_deserialize(data, size, OutPtr(invite)));
	if(not status.Ok())
	{
		return Result<OnboardingInvite>(status);
	}
	return OnboardingInvite(std::move(invite));
}

inline Result<OnboardingInvite>
OnboardingInvite::Deserialize(const std::vector<uint8_t>& wire) noexcept
{
	return Deserialize(wire.data(), wire.size());
}

inline OnboardingInviteHandle*
OnboardingInvite::Get() const noexcept
{
	return m_handle.get();
}

inline OnboardingInvite::
operator bool() const noexcept
{
	return m_handle != nullptr;
}

inline Result<std::vector<uint8_t>>
OnboardingInvite::Serialize() const noexcept
{
	size_t size = 0;
	const Status sized(qar_onboarding_invite_serialized_size(m_handle.get(), &size));
	if(not sized.Ok())
	{
		return Result<std::vector<uint8_t>>(sized);
	}
	std::vector<uint8_t> wire(size);
	size_t written = 0;
	const Status serialized(
		qar_onboarding_invite_serialize(m_handle.get(), wire.data(), wire.size(), &written)
	);
	if(not serialized.Ok())
	{
		return Result<std::vector<uint8_t>>(serialized);
	}
	wire.resize(written);
	return wire;
}

inline Result<OnboardingMethod>
OnboardingInvite::GetMethod() const noexcept
{
	OnboardingMethod method = {};
	const Status status(
		qar_onboarding_invite_get_method(m_handle.get(), detail::OutValue(&method))
	);
	if(not status.Ok())
	{
		return Result<OnboardingMethod>(status);
	}
	return method;
}

inline Result<int64_t>
OnboardingInvite::GetExpiresUnix() const noexcept
{
	int64_t expires = 0;
	const Status status(qar_onboarding_invite_get_expires_unix(m_handle.get(), &expires));
	if(not status.Ok())
	{
		return Result<int64_t>(status);
	}
	return expires;
}

inline Result<std::string>
OnboardingInvite::GetCode() const noexcept
{
	size_t length = 0;
	const Status measured(qar_onboarding_invite_get_code(m_handle.get(), nullptr, 0, &length));
	if(not measured.Ok())
	{
		return Result<std::string>(measured);
	}
	std::string code(length + 1, '\0');
	const Status read(
		qar_onboarding_invite_get_code(m_handle.get(), code.data(), code.size(), &length)
	);
	if(not read.Ok())
	{
		return Result<std::string>(read);
	}
	code.resize(length);
	return code;
}

} // namespace qaros
