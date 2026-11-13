#pragma once

// C module: detail/gui_panels.h

#include <qar_streaming_cpp/GuiPanelBuilder.hpp>
#include <qar_streaming_cpp/Subscription.hpp>
#include <qar_streaming_cpp/Values.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qaros
{

namespace detail
{
class StatusChain;
} // namespace detail

/// A GUI panel as read at one moment.
struct GuiPanel
{
	GuiPanelId Id = {};
	std::string DisplayName;
	qaros::Pose Pose = {};
	GuiPanelSize Size = {};
	GuiPanelContentType ContentType = {};
	/// Set for GuiPanelContentType::WebsiteUri.
	std::optional<std::string> ContentUri;
	/// Set for GuiPanelContentType::StreamId.
	std::optional<StreamId> ContentStreamId;
	GuiPanelState State = {};
	/// Empty means visible to every peer.
	std::vector<PeerId> VisibleToPeers;

	static Result<GuiPanel> Read(GuiPanelHandle* handle) noexcept;
	/// The update callback's handle is the library's: read, never released.
	static Result<GuiPanel> FromCallback(GuiPanelHandle* handle) noexcept;

private:
	static void
	ReadContent(GuiPanelHandle* handle, GuiPanel* panel, detail::StatusChain* chain) noexcept;
};

/// One gesture on a panel, read during its callback.
struct GuiPanelGesture
{
	PeerId SourcePeerId = {};
	uint64_t GestureId = 0;
	GuiPanelGestureKind Kind = {};
	GesturePhase Phase = {};
	GuiPanelPoint Position = {};
	GuiPanelPoint StartPosition = {};
	GuiPanelId PanelId = {};
	TimePoint Timestamp = {};
	qaros::Handedness Handedness = {};
	uint8_t TapCount = 0;

	/// The handle is valid only during the callback.
	static Result<GuiPanelGesture> FromCallback(GuiPanelGestureHandle* gesture) noexcept;
};

/// The GUI panels of a session. Keeps the session alive.
class GuiPanelControl
{
public:
	GuiPanelControl() = default;
	explicit GuiPanelControl(std::shared_ptr<SessionHandle> session) noexcept;

	[[nodiscard]]
	Result<GuiPanelId> GetOrCreate(const GuiPanelBuilder& builder) const noexcept;
	[[nodiscard]]
	Result<std::vector<GuiPanel>> GetAll() const noexcept;
	[[nodiscard]]
	Result<void> UpdatePose(const GuiPanelId& id, const Pose& pose) const noexcept;
	[[nodiscard]]
	Result<void> ChangeSize(const GuiPanelId& id, const GuiPanelSize& size) const noexcept;
	[[nodiscard]]
	Result<void> SetState(const GuiPanelId& id, GuiPanelState state) const noexcept;
	[[nodiscard]]
	Result<void> Close(const GuiPanelId& id) const noexcept;
	[[nodiscard]]
	Result<void> NavigateToUri(const GuiPanelId& id, const std::string& uri) const noexcept;
	[[nodiscard]]
	Result<void> SetStream(const GuiPanelId& id, const StreamId& stream_id) const noexcept;
	/// An empty visible-to list means visible to every peer.
	[[nodiscard]]
	Result<void> UpdateVisibleTo(
		const GuiPanelId& id,
		const std::vector<PeerId>& additions,
		const std::vector<PeerId>& removals
	) const noexcept;

	/// `fn(Result<GuiPanel>)` on a library thread for every panel change.
	template<typename Fn>
	Result<Subscription> SubscribeUpdates(Fn&& fn) const noexcept;
	/// `fn(Result<GuiPanel>)` for changes of panel `id` only.
	template<typename Fn>
	Result<Subscription> SubscribeUpdates(const GuiPanelId& id, Fn&& fn) const noexcept;
	/// `fn(Result<GuiPanelGesture>)` for one gesture kind on panel `id`.
	template<typename Fn>
	Result<Subscription>
	SubscribeGestureEvents(const GuiPanelId& id, GuiPanelGestureKind kind, Fn&& fn) const noexcept;
	/// `fn(Result<GuiPanelGesture>)` for every gesture kind on panel `id`.
	template<typename Fn>
	Result<Subscription> SubscribeAllGestureEvents(const GuiPanelId& id, Fn&& fn) const noexcept;

private:
	static std::vector<const native::PeerId*>
	Pointers(const std::vector<native::PeerId>& peers) noexcept;

	std::shared_ptr<SessionHandle> m_session;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/GuiPanels.inl>
