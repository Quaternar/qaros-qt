#pragma once

// C module: detail/app_volumes.h

#include <qar_streaming_cpp/AppVolumeBuilders.hpp>
#include <qar_streaming_cpp/Subscription.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qaros
{

namespace detail
{
class StatusChain;
} // namespace detail

/// Who may gesture on a volume and with which mode, as read at one moment.
struct AppVolumeGestureState
{
	/// Empty means every peer may.
	std::vector<PeerId> AllowedPeers;
	std::optional<PeerId> Holder;
	/// Always holds DefaultGestureModeId.
	std::vector<std::pair<AppVolumeGestureModeId, GestureConfiguration>> Modes;
	AppVolumeGestureModeId SelectedDefaultModeId = DefaultGestureModeId;
	/// One entry per peer in AppVolume::UsedByPeers.
	std::vector<std::pair<PeerId, AppVolumeGestureModeId>> ActiveModeByPeer;

	static Result<AppVolumeGestureState>
	Read(AppVolumeHandle* handle, const std::vector<PeerId>& used_by) noexcept;
};

/// An app volume as read at one moment.
struct AppVolume
{
	AppVolumeId Id = {};
	std::string DisplayName;
	qaros::Pose Pose = {};
	AppVolumeSize Size = {};
	AppVolumeLifetimeStatus LifetimeStatus = {};
	AppVolumeEditingStatus EditingStatus = {};
	std::vector<PeerId> UsedByPeers;
	qaros::Pose AppPose = {};
	float AppScale = 1.0f;
	/// Empty when the app is not anchored in the world.
	std::optional<GeoAnchorFrame> AppWorldAnchor;
	qaros::GestureConfiguration GestureConfiguration;
	AppVolumeGestureState GestureState;

	static Result<AppVolume> Read(AppVolumeHandle* handle) noexcept;
	/// The update callback's handle is the library's: read, never released.
	static Result<AppVolume> FromCallback(AppVolumeHandle* handle) noexcept;
};

/// One mapped gesture on a volume, read during its callback.
struct AppVolumeGesture
{
	PeerId SourcePeerId = {};
	AppVolumeId TargetVolumeId = {};
	GestureKind Kind = {};
	GesturePhase Phase = {};
	std::optional<Vector3> ActionPoint;
	std::optional<Vector3> StartPoint;
	Vector3 TranslationDelta = {};
	Quaternion RotationDelta = {};
	bool WasMappedToAppTransform = false;
	TimePoint Timestamp = {};
	qaros::Handedness Handedness = {};
	uint64_t GestureId = 0;
	AppVolumeGestureModeId ModeId = DefaultGestureModeId;

	/// The handle is valid only during the callback.
	static Result<AppVolumeGesture> FromCallback(AppVolumeGestureHandle* gesture) noexcept;

private:
	static void ReadPoints(
		AppVolumeGestureHandle* gesture, AppVolumeGesture* value, detail::StatusChain* chain
	) noexcept;
};

/// The app volumes of a session. Keeps the session alive.
class AppVolumeControl
{
public:
	AppVolumeControl() = default;
	explicit AppVolumeControl(std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	Result<AppVolumeId> GetOrCreate(const AppVolumeBuilder& builder) const noexcept;
	[[nodiscard]]
	Result<std::vector<AppVolume>> GetAll() const noexcept;
	[[nodiscard]]
	Result<void> Close(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<void> ChangeDisplayName(const AppVolumeId& id, const std::string& name) const noexcept;
	[[nodiscard]]
	Result<void> ChangeSize(const AppVolumeId& id, const AppVolumeSize& size) const noexcept;
	/// Low-latency fast path for the latest pose.
	[[nodiscard]]
	Result<void> ChangePose(const AppVolumeId& id, const Pose& pose) const noexcept;
	[[nodiscard]]
	Result<void> UpdateUsedByPeers(
		const AppVolumeId& id,
		const std::vector<PeerId>& additions,
		const std::vector<PeerId>& removals
	) const noexcept;
	[[nodiscard]]
	Result<void> StartEditing(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<void> StopEditing(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<void> ChangeAppPose(const AppVolumeId& id, const Pose& app_pose) const noexcept;
	[[nodiscard]]
	Result<void> ChangeAppScale(const AppVolumeId& id, float scale) const noexcept;
	[[nodiscard]]
	Result<void>
	ChangeAppWorldAnchor(const AppVolumeId& id, const GeoAnchorFrame& anchor) const noexcept;
	[[nodiscard]]
	Result<void> ClearAppWorldAnchor(const AppVolumeId& id) const noexcept;

	[[nodiscard]]
	Result<Pose> GetLatestPose(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<AppVolumeSize> GetLatestSize(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<Pose> GetLatestAppPose(const AppVolumeId& id) const noexcept;
	[[nodiscard]]
	Result<float> GetLatestAppScale(const AppVolumeId& id) const noexcept;

	[[nodiscard]]
	Result<void> ChangeGestureConfiguration(
		const AppVolumeId& id, const GestureConfigurationBuilder& configuration
	) const noexcept;
	[[nodiscard]]
	Result<void> ChangeGestureMode(
		const AppVolumeId& id,
		AppVolumeGestureModeId mode_id,
		const GestureConfigurationBuilder& configuration
	) const noexcept;
	[[nodiscard]]
	Result<void>
	RemoveGestureMode(const AppVolumeId& id, AppVolumeGestureModeId mode_id) const noexcept;
	/// `peer` empty selects the default mode for every peer without its own.
	[[nodiscard]]
	Result<void> SelectGestureMode(
		const AppVolumeId& id, const std::optional<PeerId>& peer, AppVolumeGestureModeId mode_id
	) const noexcept;
	[[nodiscard]]
	Result<void> ClearPeerGestureMode(const AppVolumeId& id, const PeerId& peer) const noexcept;
	[[nodiscard]]
	Result<void> ClearAllPeerGestureModes(const AppVolumeId& id) const noexcept;

	/// `fn(Result<AppVolume>)` on a library thread for every volume change.
	template<typename Fn>
	Result<Subscription> SubscribeUpdates(Fn&& fn) const noexcept;
	/// `fn(Result<AppVolumeGesture>)` for one gesture kind on volume `id`.
	template<typename Fn>
	Result<Subscription>
	SubscribeGestureEvents(const AppVolumeId& id, GestureKind kind, Fn&& fn) const noexcept;
	/// `fn(Result<AppVolumeGesture>)` for every gesture kind on volume `id`.
	template<typename Fn>
	Result<Subscription> SubscribeAllGestureEvents(const AppVolumeId& id, Fn&& fn) const noexcept;

private:
	template<typename T, typename Get>
	Result<T> Latest(Get get, const AppVolumeId& id) const noexcept;

	std::shared_ptr<SessionHandle> m_session;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/AppVolumes.inl>
