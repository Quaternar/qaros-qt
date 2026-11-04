#pragma once

// Builders of the runtime and onboarding inits (C module detail/onboarding.h).
// A builder owns every string and extension it chains; keep it alive until the
// call that reads it has started. Builders are not copyable: the chain points
// into them.

#include <qar_streaming_cpp/OnboardingInvite.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace qaros
{

/// The options of Runtime::Create.
class RuntimeBuilder
{
public:
	RuntimeBuilder() noexcept;
	RuntimeBuilder(const RuntimeBuilder&) = delete;
	RuntimeBuilder& operator=(const RuntimeBuilder&) = delete;

	/// The SDK `bin` folder the library was loaded from. Without it no peer
	/// can be invited.
	RuntimeBuilder& WithBinariesFolder(std::string path) noexcept;
	/// Where identities and session state persist.
	RuntimeBuilder& WithStorageFolder(std::string path) noexcept;

	[[nodiscard]]
	const native::RuntimeInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::RuntimeInit m_init;
	std::optional<std::string> m_binariesFolder;
	std::optional<std::string> m_storageFolder;
};

/// The peer presentation shared by onboard and rejoin. Unset fields stay null.
struct PresentationOptions
{
	std::optional<std::string> DisplayName;
	std::optional<std::string> AppVersion;
	std::optional<std::string> AppCustomPeerInfo;
};

/// The options of Runtime::Onboard. Exactly one mode: WithCode or WithInvite;
/// the later call replaces the earlier one.
class OnboardBuilder
{
public:
	OnboardBuilder() noexcept;
	OnboardBuilder(const OnboardBuilder&) = delete;
	OnboardBuilder& operator=(const OnboardBuilder&) = delete;

	/// Mode: the code shown on the hub's onboarding screen, or any code text
	/// the hub minted. Finds this user's hub by discovery.
	OnboardBuilder& WithCode(std::string code) noexcept;
	/// Mode: a code for the hub at `hostname:port` (plain host or IP).
	OnboardBuilder& WithCode(std::string code, std::string hostname, uint16_t port) noexcept;
	/// Mode: a full invite from a sibling instance. Borrowed: keep `invite`
	/// alive until the call has started.
	OnboardBuilder& WithInvite(const OnboardingInvite& invite) noexcept;

	/// Re-enrolls this persisted slot instead of creating a new identity.
	OnboardBuilder& WithOnboardingId(const OnboardingId& id) noexcept;
	/// Joins under this peer id instead of a fresh one; kept by every rejoin.
	OnboardBuilder& WithPeerId(const PeerId& peer_id) noexcept;
	/// Creates the session's GPU devices on this adapter.
	OnboardBuilder& WithGraphicsDevice(const GraphicsDeviceId& device) noexcept;
	OnboardBuilder& WithDisplayName(std::string name) noexcept;
	OnboardBuilder& WithAppVersion(std::string version) noexcept;
	OnboardBuilder& WithAppCustomPeerInfo(std::string info) noexcept;

	[[nodiscard]]
	const native::OnboardInit& Get() const noexcept;

private:
	enum class Mode
	{
		None,
		Code,
		Invite,
	};

	void Sync() noexcept;

	native::OnboardInit m_init;
	Mode m_mode = Mode::None;
	native::OnboardCodeExt m_code;
	std::string m_codeText;
	std::optional<native::OnboardHostExt> m_host;
	std::string m_hostname;
	native::OnboardInviteExt m_invite;
	std::optional<native::OnboardPeerIdExt> m_peerId;
	std::optional<native::GraphicsDeviceId> m_graphicsDevice;
	PresentationOptions m_presentation;
};

/// The options of Runtime::Rejoin. Unset presentation fields keep the
/// persisted values.
class RejoinBuilder
{
public:
	/// `onboarding_id` is the one a previous onboard returned.
	explicit RejoinBuilder(const OnboardingId& onboarding_id) noexcept;
	RejoinBuilder(const RejoinBuilder&) = delete;
	RejoinBuilder& operator=(const RejoinBuilder&) = delete;

	/// Creates the session's GPU devices on this adapter.
	RejoinBuilder& WithGraphicsDevice(const GraphicsDeviceId& device) noexcept;
	RejoinBuilder& WithDisplayName(std::string name) noexcept;
	RejoinBuilder& WithAppVersion(std::string version) noexcept;
	RejoinBuilder& WithAppCustomPeerInfo(std::string info) noexcept;

	[[nodiscard]]
	const native::RejoinInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::RejoinInit m_init;
	std::optional<native::GraphicsDeviceId> m_graphicsDevice;
	PresentationOptions m_presentation;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/RuntimeBuilders.inl>
