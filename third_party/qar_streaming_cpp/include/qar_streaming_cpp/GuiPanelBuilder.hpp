#pragma once

// Builder of native::GuiPanelInit and its content extension (C module
// detail/gui_panels.h). Keep it alive until the call has started; not
// copyable: the chain points into it.

#include <qar_streaming_cpp/Values.hpp>

#include <optional>
#include <string>
#include <vector>

namespace qaros
{

/// The options of GuiPanelControl::GetOrCreate. Content is WithUrl or
/// WithStream; the later call replaces the earlier one. Without either the
/// panel is a browser on the default URL.
class GuiPanelBuilder
{
public:
	/// `common_name` is the panel's ASCII identity key.
	explicit GuiPanelBuilder(std::string common_name) noexcept;
	GuiPanelBuilder(const GuiPanelBuilder&) = delete;
	GuiPanelBuilder& operator=(const GuiPanelBuilder&) = delete;

	/// UTF-8 label shown to users.
	GuiPanelBuilder& WithDisplayName(std::string name) noexcept;
	GuiPanelBuilder& WithPose(const Pose& pose) noexcept;
	GuiPanelBuilder& WithSize(const GuiPanelSize& size) noexcept;
	/// Only these peers see the panel. Empty (the default) means every peer.
	GuiPanelBuilder& WithVisibleTo(std::vector<PeerId> peers) noexcept;
	/// Content: a browser on `url`.
	GuiPanelBuilder& WithUrl(std::string url) noexcept;
	/// Content: video stream `stream_id`.
	GuiPanelBuilder& WithStream(const StreamId& stream_id) noexcept;

	[[nodiscard]]
	const native::GuiPanelInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::GuiPanelInit m_init;
	std::string m_commonName;
	std::optional<std::string> m_displayName;
	std::vector<native::PeerId> m_visibleTo;
	std::optional<native::GuiPanelUrlExt> m_url;
	std::string m_urlText;
	std::optional<native::GuiPanelStreamIdExt> m_stream;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/GuiPanelBuilder.inl>
