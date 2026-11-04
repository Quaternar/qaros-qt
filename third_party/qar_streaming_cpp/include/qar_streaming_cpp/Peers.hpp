#pragma once

// C module: detail/peer_management.h

#include <qar_streaming_cpp/Subscription.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <memory>
#include <string>
#include <vector>

namespace qaros
{

/// A peer as read at one moment.
struct PeerSpec
{
	PeerId Id = {};
	std::string DisplayName;
	std::string AppVersion;
	std::string AppCustomPeerInfo;
	qaros::AppState AppState = qaros::AppState::Unknown;
	std::string VersionId;
	std::string RoomTag;
	bool IsTargetApp = false;

	static Result<PeerSpec> Read(PeerSpecHandle* handle) noexcept;
	/// The update callback's handle is the library's: read, never released.
	static Result<PeerSpec> FromCallback(PeerSpecHandle* handle) noexcept;
};

/// The peers of a session. Keeps the session alive.
class PeerControl
{
public:
	PeerControl() = default;
	explicit PeerControl(std::shared_ptr<SessionHandle> session) noexcept;

	/// The session's own spec as this process knows it (see the C API).
	[[nodiscard]]
	Result<PeerSpec> GetMySpec() const noexcept;
	[[nodiscard]]
	Result<std::vector<PeerSpec>> GetAll() const noexcept;
	[[nodiscard]]
	Result<void> UpdateDisplayName(const std::string& name) const noexcept;

	/// `fn(Result<PeerSpec>)` on a library thread for every peer change.
	template<typename Fn>
	Result<Subscription> SubscribeUpdates(Fn&& fn) const noexcept;

private:
	std::shared_ptr<SessionHandle> m_session;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/Peers.inl>
