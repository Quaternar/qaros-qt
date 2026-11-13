#pragma once

// C modules: detail/session.h, the session calls of detail/onboarding.h and
// detail/target_apps.h. Per-module calls hang off the controls it hands out.
// Async methods: see Async.hpp.

#include <qar_streaming_cpp/AppVolumes.hpp>
#include <qar_streaming_cpp/Async.hpp>
#include <qar_streaming_cpp/GuiPanels.hpp>
#include <qar_streaming_cpp/OnboardingInvite.hpp>
#include <qar_streaming_cpp/Peers.hpp>
#include <qar_streaming_cpp/RenderSender.hpp>
#include <qar_streaming_cpp/VideoSender.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qaros
{

/// A joined session. Copies share the handle; the session ends when the last
/// copy, control, sender or pending create referring to it goes.
///
/// Never let the last reference go inside one of its own callbacks: the C API
/// forbids destroying a session from a callback.
class Session
{
public:
	Session() noexcept = default;
	explicit Session(SessionPtr owned) noexcept;

	[[nodiscard]]
	SessionHandle* Get() const noexcept;
	explicit operator bool() const noexcept;

	[[nodiscard]]
	PeerControl Peers() const noexcept;
	[[nodiscard]]
	GuiPanelControl GuiPanels() const noexcept;
	[[nodiscard]]
	AppVolumeControl AppVolumes() const noexcept;
	[[nodiscard]]
	RenderSenderControl RenderSenders() const noexcept;
	[[nodiscard]]
	VideoSenderControl VideoSenders() const noexcept;

	[[nodiscard]]
	Result<SessionId> GetId() const noexcept;
	/// Empty while the session does not know its adapter.
	[[nodiscard]]
	Result<std::optional<GraphicsDeviceId>> GetGraphicsDeviceId() const noexcept;
	[[nodiscard]]
	Result<PeerId> FindRenderTarget(const std::string& connection) const noexcept;

	/// A fresh invite from the hub this session onboarded through.
	[[nodiscard]]
	Result<OnboardingInvite>
	RequestOnboardingInvite(const CallOptions& options = {}) const noexcept;
	/// Completes with Result<OnboardingInvite>.
	template<typename Token = UseFutureT>
	auto
	RequestOnboardingInviteAsync(CallOptions options = {}, Token&& token = Token{}) const noexcept;
	/// `count` one-time codes (1 to 64), each its own invite, valid for
	/// `lifetime` (0 means 120 s, at most 600 s). With `planned_peer`, a device
	/// pairing with one can only join under that peer id.
	[[nodiscard]]
	Result<std::vector<OnboardingInvite>> RequestOnboardingCodes(
		uint32_t count,
		std::chrono::seconds lifetime = std::chrono::seconds(0),
		const std::optional<PeerId>& planned_peer = std::nullopt,
		const CallOptions& options = {}
	) const noexcept;

	/// Starts the target app named by `connection_string` (see
	/// TargetAppConnectionString); its peer id.
	[[nodiscard]]
	Result<PeerId> InviteTargetApp(
		const std::string& connection_string, const CallOptions& options = {}
	) const noexcept;
	/// Completes with Result<PeerId>, the invited target app.
	template<typename Token = UseFutureT>
	auto InviteTargetAppAsync(
		std::string connection_string, CallOptions options = {}, Token&& token = Token{}
	) const noexcept;
	[[nodiscard]]
	Result<void>
	KillTargetApp(const PeerId& target_app, const CallOptions& options = {}) const noexcept;
	/// Completes with Result<PeerId>, the killed target app.
	template<typename Token = UseFutureT>
	auto KillTargetAppAsync(
		PeerId target_app, CallOptions options = {}, Token&& token = Token{}
	) const noexcept;

private:
	std::shared_ptr<SessionHandle> m_handle;
};

/// Connection strings for Session::InviteTargetApp.
struct TargetAppConnectionString
{
	static Result<std::string> HoloLens(const std::string& host) noexcept;
	static Result<std::string> Quest(const std::string& host) noexcept;
	static Result<std::string> Android(const std::string& host) noexcept;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/Session.inl>
