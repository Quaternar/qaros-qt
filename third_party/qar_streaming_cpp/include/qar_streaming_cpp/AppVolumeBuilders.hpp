#pragma once

// Builders of native::AppVolumeInit and native::AppVolumeGestureConfiguration with its
// allowed-peers extension (C module detail/app_volumes.h). Keep a builder alive
// until the call has started.

#include <qar_streaming_cpp/Values.hpp>

#include <optional>
#include <string>
#include <vector>

namespace qaros
{

/// How the controller ray of a gesture is drawn.
struct ControllerRayStyle
{
	AppVolumeControllerRayLineStyle LineStyle = {};
	qaros::Color Color = {};
};

/// Which gesture moves or scales the app, and how. Start from
/// LibraryDefault() and change what differs.
struct AppVolumeGestureMappingRule
{
	GestureKind Kind = {};
	bool Enabled = false;
	float Precision = 0.0f;
	AppScaleSensitivityMode AppScaleSensitivity = {};
	AppVolumeAxisFlags TranslationAxes = AppVolumeAxisFlags::None;
	AppVolumeAxisFlags RotationAxes = AppVolumeAxisFlags::None;
	bool EnableGesturesFromOutsideOfVolume = false;
	ControllerRayStyle ControllerRay;
	GestureAppTransformMapping AppTransformMapping = {};

	/// The rule the C API starts every mapping from.
	static AppVolumeGestureMappingRule LibraryDefault() noexcept;
};

native::AppVolumeGestureMappingRule ToNative(const AppVolumeGestureMappingRule& rule) noexcept;
AppVolumeGestureMappingRule FromNative(const native::AppVolumeGestureMappingRule& rule) noexcept;

/// A gesture configuration as read at one moment: its rules, highest
/// priority first.
struct GestureConfiguration
{
	std::vector<AppVolumeGestureMappingRule> Rules;
};

GestureConfiguration
FromNative(const native::AppVolumeGestureConfiguration& configuration) noexcept;

/// A gesture configuration: mapping rules in descending priority, and the
/// peers whose gestures may move the app. Starts from the library's default
/// rules. Copyable: a copy relinks to its own storage.
class GestureConfigurationBuilder
{
public:
	GestureConfigurationBuilder() noexcept;
	GestureConfigurationBuilder(const GestureConfigurationBuilder& other) noexcept;
	GestureConfigurationBuilder& operator=(const GestureConfigurationBuilder& other) noexcept;

	/// Replaces every rule; element 0 has the highest priority.
	GestureConfigurationBuilder& WithRules(std::vector<AppVolumeGestureMappingRule> rules) noexcept;
	/// Appends a rule below the existing ones.
	GestureConfigurationBuilder& AddRule(const AppVolumeGestureMappingRule& rule) noexcept;
	/// Only these peers' gestures move or scale the app. Empty (the default)
	/// means every peer's.
	GestureConfigurationBuilder& WithAllowedPeers(std::vector<PeerId> peers) noexcept;

	/// Fails when there are more than MaxGestureMappingRules rules.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
	[[nodiscard]]
	const native::AppVolumeGestureConfiguration& Get() const noexcept;

private:
	void Sync() noexcept;

	native::AppVolumeGestureConfiguration m_config;
	std::vector<AppVolumeGestureMappingRule> m_rules;
	std::optional<native::AppVolumeGestureAllowedPeersExt> m_allowed;
	std::vector<native::PeerId> m_allowedPeers;
};

/// The options of AppVolumeControl::GetOrCreate.
class AppVolumeBuilder
{
public:
	/// `common_name` is the volume's ASCII identity key.
	explicit AppVolumeBuilder(std::string common_name) noexcept;
	AppVolumeBuilder(const AppVolumeBuilder&) = delete;
	AppVolumeBuilder& operator=(const AppVolumeBuilder&) = delete;

	/// UTF-8 label shown to users.
	AppVolumeBuilder& WithDisplayName(std::string name) noexcept;
	/// The cuboid's centre and rotation in room space.
	AppVolumeBuilder& WithPose(const Pose& pose) noexcept;
	/// Cuboid extents in metres.
	AppVolumeBuilder& WithSize(const AppVolumeSize& size) noexcept;
	AppVolumeBuilder& WithInitialPeers(std::vector<PeerId> peers) noexcept;
	/// Origin of the application inside the volume, in volume metres.
	AppVolumeBuilder& WithAppPose(const Pose& app_pose) noexcept;
	/// Room metres per app metre.
	AppVolumeBuilder& WithAppScale(float scale) noexcept;
	AppVolumeBuilder& WithAppWorldAnchor(const GeoAnchorFrame& anchor) noexcept;
	AppVolumeBuilder& WithGestureConfiguration(GestureConfigurationBuilder configuration) noexcept;

	/// Fails when the gesture configuration does.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
	[[nodiscard]]
	const native::AppVolumeInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::AppVolumeInit m_init;
	std::string m_commonName;
	std::optional<std::string> m_displayName;
	std::vector<native::PeerId> m_initialPeers;
	std::vector<const native::PeerId*> m_initialPeerPointers;
	std::optional<GestureConfigurationBuilder> m_gestureConfiguration;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/AppVolumeBuilders.inl>
