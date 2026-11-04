#pragma once

// Implementation of AppVolumeBuilders.hpp.

#include <qar_streaming_cpp/detail/Chain.hpp>
#include <qar_streaming_cpp/detail/Native.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace qaros
{

// Value structs

inline AppVolumeGestureMappingRule
AppVolumeGestureMappingRule::LibraryDefault() noexcept
{
	return FromNative(qar_app_volume_gesture_mapping_rule_default());
}

inline native::AppVolumeGestureMappingRule
ToNative(const AppVolumeGestureMappingRule& rule) noexcept
{
	native::AppVolumeGestureMappingRule native = qar_app_volume_gesture_mapping_rule_default();
	native.gesture_kind = ToNative(rule.Kind);
	native.enabled = rule.Enabled;
	native.precision = rule.Precision;
	native.app_scale_sensitivity_mode = ToNative(rule.AppScaleSensitivity);
	native.translation_axes = static_cast<uint32_t>(ToNative(rule.TranslationAxes));
	native.rotation_axes = static_cast<uint32_t>(ToNative(rule.RotationAxes));
	native.enable_gestures_from_outside_of_volume = rule.EnableGesturesFromOutsideOfVolume;
	native.controller_ray_style.line_style = ToNative(rule.ControllerRay.LineStyle);
	native.controller_ray_style.color = ToNative(rule.ControllerRay.Color);
	native.app_transform_mapping = ToNative(rule.AppTransformMapping);
	return native;
}

inline AppVolumeGestureMappingRule
FromNative(const native::AppVolumeGestureMappingRule& rule) noexcept
{
	AppVolumeGestureMappingRule value;
	value.Kind = FromNative(rule.gesture_kind);
	value.Enabled = rule.enabled;
	value.Precision = rule.precision;
	value.AppScaleSensitivity = FromNative(rule.app_scale_sensitivity_mode);
	value.TranslationAxes = static_cast<AppVolumeAxisFlags>(rule.translation_axes);
	value.RotationAxes = static_cast<AppVolumeAxisFlags>(rule.rotation_axes);
	value.EnableGesturesFromOutsideOfVolume = rule.enable_gestures_from_outside_of_volume;
	value.ControllerRay.LineStyle = FromNative(rule.controller_ray_style.line_style);
	value.ControllerRay.Color = FromNative(rule.controller_ray_style.color);
	value.AppTransformMapping = FromNative(rule.app_transform_mapping);
	return value;
}

inline GestureConfiguration
FromNative(const native::AppVolumeGestureConfiguration& configuration) noexcept
{
	GestureConfiguration value;
	const size_t count = std::min(configuration.mapping_rule_count, MaxGestureMappingRules);
	for(size_t index = 0; index < count; ++index)
	{
		value.Rules.push_back(FromNative(configuration.mapping_rules[index]));
	}
	return value;
}

// GestureConfigurationBuilder

inline GestureConfigurationBuilder::GestureConfigurationBuilder() noexcept
	: m_config(native::Default<native::AppVolumeGestureConfiguration>())
	, m_rules(FromNative(m_config).Rules)
{
	Sync();
}

inline GestureConfigurationBuilder::GestureConfigurationBuilder(
	const GestureConfigurationBuilder& other
) noexcept
	: m_config(other.m_config)
	, m_rules(other.m_rules)
	, m_allowed(other.m_allowed)
	, m_allowedPeers(other.m_allowedPeers)
{
	Sync();
}

inline GestureConfigurationBuilder&
GestureConfigurationBuilder::operator=(const GestureConfigurationBuilder& other) noexcept
{
	if(this != &other)
	{
		m_config = other.m_config;
		m_rules = other.m_rules;
		m_allowed = other.m_allowed;
		m_allowedPeers = other.m_allowedPeers;
		Sync();
	}
	return *this;
}

inline GestureConfigurationBuilder&
GestureConfigurationBuilder::WithRules(std::vector<AppVolumeGestureMappingRule> rules) noexcept
{
	m_rules = std::move(rules);
	Sync();
	return *this;
}

inline GestureConfigurationBuilder&
GestureConfigurationBuilder::AddRule(const AppVolumeGestureMappingRule& rule) noexcept
{
	m_rules.push_back(rule);
	Sync();
	return *this;
}

inline GestureConfigurationBuilder&
GestureConfigurationBuilder::WithAllowedPeers(std::vector<PeerId> peers) noexcept
{
	m_allowedPeers = detail::ToNativeAll(peers);
	m_allowed = native::Default<native::AppVolumeGestureAllowedPeersExt>();
	Sync();
	return *this;
}

inline Result<void>
GestureConfigurationBuilder::Validate() const noexcept
{
	if(m_rules.size() > MaxGestureMappingRules)
	{
		return Result<void>(Status::Error(
			StatusCode::ArgumentNotSupported,
			"gesture configuration has more mapping rules than MaxGestureMappingRules"
		));
	}
	return Result<void>();
}

inline const native::AppVolumeGestureConfiguration&
GestureConfigurationBuilder::Get() const noexcept
{
	return m_config;
}

inline void
GestureConfigurationBuilder::Sync() noexcept
{
	const size_t count = std::min(m_rules.size(), MaxGestureMappingRules);
	for(size_t index = 0; index < count; ++index)
	{
		m_config.mapping_rules[index] = ToNative(m_rules[index]);
	}
	m_config.mapping_rule_count = count;
	if(m_allowed.has_value())
	{
		m_allowed->allowed_peers = m_allowedPeers.empty() ? nullptr : m_allowedPeers.data();
		m_allowed->allowed_peer_count = m_allowedPeers.size();
	}
	detail::Link(&m_config.header, { detail::OptionalHeader(m_allowed) });
}

// AppVolumeBuilder

inline AppVolumeBuilder::AppVolumeBuilder(std::string common_name) noexcept
	: m_init(native::Default<native::AppVolumeInit>())
	, m_commonName(std::move(common_name))
{
	Sync();
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithDisplayName(std::string name) noexcept
{
	m_displayName = std::move(name);
	Sync();
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithPose(const Pose& pose) noexcept
{
	m_init.pose = ToNative(pose);
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithSize(const AppVolumeSize& size) noexcept
{
	m_init.size = ToNative(size);
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithInitialPeers(std::vector<PeerId> peers) noexcept
{
	m_initialPeers = detail::ToNativeAll(peers);
	Sync();
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithAppPose(const Pose& app_pose) noexcept
{
	m_init.app_pose = ToNative(app_pose);
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithAppScale(float scale) noexcept
{
	m_init.app_scale = scale;
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithAppWorldAnchor(const GeoAnchorFrame& anchor) noexcept
{
	m_init.has_app_world_anchor = true;
	m_init.app_world_anchor = ToNative(anchor);
	return *this;
}

inline AppVolumeBuilder&
AppVolumeBuilder::WithGestureConfiguration(GestureConfigurationBuilder configuration) noexcept
{
	m_gestureConfiguration = std::move(configuration);
	Sync();
	return *this;
}

inline Result<void>
AppVolumeBuilder::Validate() const noexcept
{
	if(not m_gestureConfiguration.has_value())
	{
		return Result<void>();
	}
	return m_gestureConfiguration->Validate();
}

inline const native::AppVolumeInit&
AppVolumeBuilder::Get() const noexcept
{
	return m_init;
}

inline void
AppVolumeBuilder::Sync() noexcept
{
	m_init.common_name = m_commonName.c_str();
	if(m_displayName.has_value())
	{
		m_init.display_name = m_displayName->c_str();
	}
	m_initialPeerPointers.clear();
	for(const native::PeerId& peer : m_initialPeers)
	{
		m_initialPeerPointers.push_back(&peer);
	}
	m_init.initial_peers = m_initialPeerPointers.empty() ? nullptr : m_initialPeerPointers.data();
	m_init.initial_peer_count = m_initialPeerPointers.size();
	m_init.gesture_configuration =
		m_gestureConfiguration.has_value() ? &m_gestureConfiguration->Get() : nullptr;
}

} // namespace qaros
