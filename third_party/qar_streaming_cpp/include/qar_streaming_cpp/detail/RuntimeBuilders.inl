#pragma once

// Implementation of RuntimeBuilders.hpp.

#include <qar_streaming_cpp/detail/Chain.hpp>

#include <utility>

namespace qaros
{

namespace detail
{

inline void
ApplyPresentation(
	const PresentationOptions& options, native::PeerPresentation* presentation
) noexcept
{
	presentation->display_name = OptionalText(options.DisplayName);
	presentation->app_version = OptionalText(options.AppVersion);
	presentation->app_custom_peer_info = OptionalText(options.AppCustomPeerInfo);
}

} // namespace detail

// RuntimeBuilder

inline RuntimeBuilder::RuntimeBuilder() noexcept
	: m_init(native::Default<native::RuntimeInit>())
{
}

inline RuntimeBuilder&
RuntimeBuilder::WithBinariesFolder(std::string path) noexcept
{
	m_binariesFolder = std::move(path);
	Sync();
	return *this;
}

inline RuntimeBuilder&
RuntimeBuilder::WithStorageFolder(std::string path) noexcept
{
	m_storageFolder = std::move(path);
	Sync();
	return *this;
}

inline const native::RuntimeInit&
RuntimeBuilder::Get() const noexcept
{
	return m_init;
}

inline void
RuntimeBuilder::Sync() noexcept
{
	if(m_binariesFolder.has_value())
	{
		m_init.runtime_binaries_folder_path = m_binariesFolder->c_str();
	}
	if(m_storageFolder.has_value())
	{
		m_init.storage_folder_path = m_storageFolder->c_str();
	}
}

// OnboardBuilder

inline OnboardBuilder::OnboardBuilder() noexcept
	: m_init(native::Default<native::OnboardInit>())
	, m_code(native::Default<native::OnboardCodeExt>())
	, m_invite(native::Default<native::OnboardInviteExt>())
{
}

inline OnboardBuilder&
OnboardBuilder::WithCode(std::string code) noexcept
{
	m_mode = Mode::Code;
	m_codeText = std::move(code);
	m_host.reset();
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithCode(std::string code, std::string hostname, uint16_t port) noexcept
{
	m_mode = Mode::Code;
	m_codeText = std::move(code);
	m_hostname = std::move(hostname);
	m_host = native::Default<native::OnboardHostExt>();
	m_host->port = port;
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithInvite(const OnboardingInvite& invite) noexcept
{
	m_mode = Mode::Invite;
	m_invite.invite = invite.Get();
	m_host.reset();
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithOnboardingId(const OnboardingId& id) noexcept
{
	m_init.onboarding_id = ToNative(id);
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithPeerId(const PeerId& peer_id) noexcept
{
	m_peerId = native::Default<native::OnboardPeerIdExt>();
	m_peerId->peer_id = ToNative(peer_id);
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithGraphicsDevice(const GraphicsDeviceId& device) noexcept
{
	m_graphicsDevice = ToNative(device);
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithDisplayName(std::string name) noexcept
{
	m_presentation.DisplayName = std::move(name);
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithAppVersion(std::string version) noexcept
{
	m_presentation.AppVersion = std::move(version);
	Sync();
	return *this;
}

inline OnboardBuilder&
OnboardBuilder::WithAppCustomPeerInfo(std::string info) noexcept
{
	m_presentation.AppCustomPeerInfo = std::move(info);
	Sync();
	return *this;
}

inline const native::OnboardInit&
OnboardBuilder::Get() const noexcept
{
	return m_init;
}

inline void
OnboardBuilder::Sync() noexcept
{
	detail::ApplyPresentation(m_presentation, &m_init.presentation);
	m_code.code = m_codeText.c_str();
	if(m_host.has_value())
	{
		m_host->hostname = m_hostname.c_str();
	}
	native::StructureHeader* mode = nullptr;
	if(m_mode == Mode::Code)
	{
		mode = &m_code.header;
	}
	if(m_mode == Mode::Invite)
	{
		mode = &m_invite.header;
	}
	detail::Link(
		&m_init.header,
		{ mode,
		  detail::OptionalHeader(m_host),
		  detail::OptionalHeader(m_peerId),
		  detail::OptionalHeader(m_graphicsDevice) }
	);
}

// RejoinBuilder

inline RejoinBuilder::RejoinBuilder(const OnboardingId& onboarding_id) noexcept
	: m_init(native::Default<native::RejoinInit>())
{
	m_init.onboarding_id = ToNative(onboarding_id);
}

inline RejoinBuilder&
RejoinBuilder::WithGraphicsDevice(const GraphicsDeviceId& device) noexcept
{
	m_graphicsDevice = ToNative(device);
	Sync();
	return *this;
}

inline RejoinBuilder&
RejoinBuilder::WithDisplayName(std::string name) noexcept
{
	m_presentation.DisplayName = std::move(name);
	Sync();
	return *this;
}

inline RejoinBuilder&
RejoinBuilder::WithAppVersion(std::string version) noexcept
{
	m_presentation.AppVersion = std::move(version);
	Sync();
	return *this;
}

inline RejoinBuilder&
RejoinBuilder::WithAppCustomPeerInfo(std::string info) noexcept
{
	m_presentation.AppCustomPeerInfo = std::move(info);
	Sync();
	return *this;
}

inline const native::RejoinInit&
RejoinBuilder::Get() const noexcept
{
	return m_init;
}

inline void
RejoinBuilder::Sync() noexcept
{
	detail::ApplyPresentation(m_presentation, &m_init.presentation);
	detail::Link(&m_init.header, { detail::OptionalHeader(m_graphicsDevice) });
}

} // namespace qaros
