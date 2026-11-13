#pragma once

// Implementation of GuiPanelBuilder.hpp.

#include <qar_streaming_cpp/detail/Chain.hpp>
#include <qar_streaming_cpp/detail/Native.hpp>

#include <utility>

namespace qaros
{

inline GuiPanelBuilder::GuiPanelBuilder(std::string common_name) noexcept
	: m_init(native::Default<native::GuiPanelInit>())
	, m_commonName(std::move(common_name))
{
	Sync();
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithDisplayName(std::string name) noexcept
{
	m_displayName = std::move(name);
	Sync();
	return *this;
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithPose(const Pose& pose) noexcept
{
	m_init.pose = ToNative(pose);
	return *this;
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithSize(const GuiPanelSize& size) noexcept
{
	m_init.size = ToNative(size);
	return *this;
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithVisibleTo(std::vector<PeerId> peers) noexcept
{
	m_visibleTo = detail::ToNativeAll(peers);
	Sync();
	return *this;
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithUrl(std::string url) noexcept
{
	m_urlText = std::move(url);
	m_url = native::Default<native::GuiPanelUrlExt>();
	m_stream.reset();
	Sync();
	return *this;
}

inline GuiPanelBuilder&
GuiPanelBuilder::WithStream(const StreamId& stream_id) noexcept
{
	m_stream = native::Default<native::GuiPanelStreamIdExt>();
	m_stream->stream_id = ToNative(stream_id);
	m_url.reset();
	Sync();
	return *this;
}

inline const native::GuiPanelInit&
GuiPanelBuilder::Get() const noexcept
{
	return m_init;
}

inline void
GuiPanelBuilder::Sync() noexcept
{
	m_init.common_name = m_commonName.c_str();
	if(m_displayName.has_value())
	{
		m_init.display_name = m_displayName->c_str();
	}
	m_init.visible_to_peers = m_visibleTo.empty() ? nullptr : m_visibleTo.data();
	m_init.visible_to_peer_count = m_visibleTo.size();
	if(m_url.has_value())
	{
		m_url->url = m_urlText.c_str();
	}
	native::StructureHeader* content = nullptr;
	if(m_url.has_value())
	{
		content = &m_url->header;
	}
	if(m_stream.has_value())
	{
		content = &m_stream->header;
	}
	detail::Link(&m_init.header, { content });
}

} // namespace qaros
