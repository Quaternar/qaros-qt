#pragma once

// The C++ value types of the API: plain structs with the C API's defaults,
// converted at the C boundary with ToNative / FromNative (NativeOf<T> names the
// C struct). Defaults the C API does not define are marked "C++ default".

#include <qar_streaming_cpp/Status.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace qaros
{

/// A 16-byte id. `Tag` makes every kind of id its own type.
template<typename Tag>
struct Id
{
	std::array<uint8_t, QAR_MAX_ID_LENGTH> Bytes = {};

	/// The zero id: "none" wherever the C API takes an optional id.
	[[nodiscard]]
	bool IsZero() const noexcept;
	/// The canonical UUID text.
	[[nodiscard]]
	Result<std::string> ToString() const noexcept;
	static Result<Id> FromString(const std::string& text) noexcept;
};

template<typename Tag>
bool operator==(const Id<Tag>& left, const Id<Tag>& right) noexcept;
template<typename Tag>
bool operator!=(const Id<Tag>& left, const Id<Tag>& right) noexcept;
template<typename Tag>
bool operator<(const Id<Tag>& left, const Id<Tag>& right) noexcept;

using AppVolumeId = Id<struct AppVolumeIdTag>;
using GuiPanelId = Id<struct GuiPanelIdTag>;
using OnboardingId = Id<struct OnboardingIdTag>;
using PeerId = Id<struct PeerIdTag>;
using SessionId = Id<struct SessionIdTag>;
using StreamId = Id<struct StreamIdTag>;

struct Vector3
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
};

struct Vector3d
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
};

/// Identity by default.
struct Quaternion
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float W = 1.0f;
};

/// Identity by default.
struct Pose
{
	Quaternion Orientation;
	Vector3 Position;
};

/// Half-angles in radians; 90 degrees wide and high by default.
struct Fov
{
	float AngleLeft = -0.785f;
	float AngleRight = 0.785f;
	float AngleUp = 0.785f;
	float AngleDown = -0.785f;
};

/// Wall-clock instant: nanoseconds since the Unix epoch (UTC). Instants from
/// different hosts compare only as well as their clocks are synchronized.
struct TimePoint
{
	std::chrono::nanoseconds SinceUnixEpoch{ 0 };
};

/// Opaque white by default (C++ default).
struct Color
{
	uint8_t R = 255;
	uint8_t G = 255;
	uint8_t B = 255;
	uint8_t A = 255;
};

struct GuiPanelPoint
{
	int32_t XPixels = 0;
	int32_t YPixels = 0;
};

struct GuiPanelSize
{
	float WidthMeters = 1.0f;
	float HeightMeters = 1.0f;
	float ContentScale = 1.0f;
};

/// Extents along app-volume-space X / Z / Y, in metres.
struct AppVolumeSize
{
	float WidthMeters = 1.0f;
	float LengthMeters = 1.0f;
	float HeightMeters = 1.0f;
};

struct AppVolumeEditingStatus
{
	bool IsBeingEdited = false;
	PeerId Editor;
};

/// A local frame anchored in a world reference system. Axes are unit
/// vectors; handedness must be Left or Right (C++ default: Right).
struct GeoAnchorFrame
{
	WorldReferenceSystem ReferenceSystem = WorldReferenceSystem::EcefWGS84Meters;
	Vector3d Origin;
	Vector3d AxisX{ 1.0, 0.0, 0.0 };
	Vector3d AxisY{ 0.0, 1.0, 0.0 };
	Vector3d AxisZ{ 0.0, 0.0, 1.0 };
	qaros::Handedness Handedness = qaros::Handedness::Right;
};

/// The adapter a session creates its GPU devices on.
struct GraphicsDeviceId
{
#ifdef _WIN32
	GpuDeviceIdType Type = GpuDeviceIdType::Luid;
#else
	GpuDeviceIdType Type = GpuDeviceIdType::Uuid;
#endif
	std::array<uint8_t, 8> Luid = {};
	std::array<uint8_t, 16> Uuid = {};
};

struct TextureSize
{
	PixelFormat Format = PixelFormat::B8G8R8A8;
	uint32_t Width = 0;
	uint32_t Height = 0;
	uint32_t ArrayLayers = 1;
};

/// A view's rectangle inside a texture and what it shows.
struct VideoFrameView
{
	uint32_t StartX = 0;
	uint32_t StartY = 0;
	uint32_t EndX = 0;
	uint32_t EndY = 0;
	uint32_t TextureIndex = 0;
	uint32_t ArrayLayerIndex = 0;
	PixelFormat TextureFormat = PixelFormat::B8G8R8A8;
	VideoFrameViewType Type = VideoFrameViewType::Color;
	VideoFrameViewEye Eye = VideoFrameViewEye::None;
};

/// The views of a frame and the textures they live in.
struct VideoFrameLayout
{
	std::vector<VideoFrameView> Views;
	std::vector<TextureSize> Textures;

	/// Fails with more than MaxFrameViews views or MaxFrameTextures textures.
	[[nodiscard]]
	Result<void> Validate() const noexcept;
};

/// A CPU texture lent by the library; `Data` is valid until ShowFrame.
struct VideoTextureCpu
{
	TextureSize Size;
	uint32_t Pitch = 0;
	uint8_t* Data = nullptr;
	size_t DataSize = 0;
};

struct VideoFrameCpu
{
	std::vector<VideoFrameView> TextureViews;
	std::vector<VideoTextureCpu> Textures;
};

#ifdef QAR_ENABLE_D3D11
struct VideoTextureD3D11
{
	TextureSize Size;
	ID3D11Texture2D* Texture = nullptr;
};

struct KeyedMutexSync
{
	IDXGIKeyedMutex* Mutex = nullptr;
	uint32_t Acquire = 0;
	uint32_t Release = 0;
};

struct VideoFrameD3D11
{
	std::vector<VideoFrameView> TextureViews;
	std::vector<VideoTextureD3D11> Textures;
	/// One per texture.
	std::vector<KeyedMutexSync> TextureSync;
};

/// The D3D11 device a sender renders with.
struct D3D11Device
{
	ID3D11Device1* Device = nullptr;
	ID3D11DeviceContext1* Context = nullptr;
	uint32_t ColorBindFlags = 0;
	uint32_t DepthBindFlags = 0;
	bool AcquireKeyedMutexSync = false;
};
#endif

#ifdef QAR_ENABLE_OPENGL
struct VideoTextureOpenGl
{
	TextureSize Size;
	uint32_t Texture = 0;
};

struct VideoFrameOpenGl
{
	std::vector<VideoFrameView> TextureViews;
	std::vector<VideoTextureOpenGl> Textures;
};

/// How the library reaches the application's OpenGL context (see the C API).
struct OpenGlContextCallbacks
{
	void* UserState = nullptr;
	qar_opengl_get_proc_address_callback_t GetProcAddress = nullptr;
	qar_opengl_context_action_callback_t MakeCurrent = nullptr;
	qar_opengl_capture_current_callback_t CaptureCurrent = nullptr;
	qar_opengl_restore_current_callback_t RestoreCurrent = nullptr;
};

struct OpenGlEglContext
{
	void* Display = nullptr;
	uintptr_t DrawSurface = 0;
	uintptr_t ReadSurface = 0;
	void* Context = nullptr;
	OpenGlContextCallbacks Callbacks;
};

struct OpenGlWglContext
{
	void* DeviceContext = nullptr;
	void* Context = nullptr;
	OpenGlContextCallbacks Callbacks;
};

struct OpenGlGlxContext
{
	void* Display = nullptr;
	uintptr_t Drawable = 0;
	void* Context = nullptr;
	OpenGlContextCallbacks Callbacks;
};
#endif

#ifdef QAR_ENABLE_VULKAN
struct VideoTextureVulkan
{
	TextureSize Size;
	VkImage Image = VK_NULL_HANDLE;
	VkDeviceMemory Memory = VK_NULL_HANDLE;
	VkFormatFeatureFlags FormatFeatures = 0;
	VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	uint32_t QueueFamilyIndex = 0;
};

/// The library's Vulkan frame, valid until the next GetVulkanFrame or the
/// sender's destruction. Not a copy: the layouts and queue families written
/// with SetTextureState are what the library reads at ShowFrame.
class VulkanFrame
{
public:
	VulkanFrame() noexcept = default;
	explicit VulkanFrame(native::VideoFrameVulkan* frame) noexcept;

	[[nodiscard]]
	std::vector<VideoFrameView> TextureViews() const noexcept;
	[[nodiscard]]
	size_t TextureCount() const noexcept;
	[[nodiscard]]
	VideoTextureVulkan Texture(size_t index) const noexcept;
	/// Records the layout and queue family the image is left in.
	void SetTextureState(size_t index, VkImageLayout layout, uint32_t queue_family_index) noexcept;
	[[nodiscard]]
	VkSemaphore Semaphore() const noexcept;
	[[nodiscard]]
	VkFence Fence() const noexcept;
	[[nodiscard]]
	native::VideoFrameVulkan* Native() const noexcept;

private:
	native::VideoFrameVulkan* m_frame = nullptr;
};
#endif

struct HandJointLocation
{
	HandJoint Joint = HandJoint::Palm;
	uint64_t LocationFlags = 0;
	qaros::Pose Pose;
	float Radius = 0.0f;
};

struct HandJointVelocity
{
	HandJoint Joint = HandJoint::Palm;
	uint64_t Flags = 0;
	Vector3 LinearVelocity;
	Vector3 AngularVelocity;
};

struct HandJoints
{
	bool IsTracked = false;
	bool IsActive = false;
	qaros::Pose Pose;
	std::array<HandJointLocation, HandJointCount> JointLocations = {};
	bool HasVelocity = false;
	std::array<HandJointVelocity, HandJointCount> JointVelocities = {};
};

struct DeviceHands
{
	PeerId Device;
	TimePoint DeviceTimestamp;
	HandJoints Left;
	HandJoints Right;
};

// Conversions. FromNative takes the C struct, ToNative returns it; a C++ type
// with fixed-size C arrays behind it is cut to the C capacity (Validate first).

template<typename Tag>
typename NativeOf<Id<Tag>>::Type ToNative(const Id<Tag>& id) noexcept;
AppVolumeId FromNative(const native::AppVolumeId& id) noexcept;
GuiPanelId FromNative(const native::GuiPanelId& id) noexcept;
OnboardingId FromNative(const native::OnboardingId& id) noexcept;
PeerId FromNative(const native::PeerId& id) noexcept;
SessionId FromNative(const native::SessionId& id) noexcept;
StreamId FromNative(const native::StreamId& id) noexcept;

native::Vector3 ToNative(const Vector3& value) noexcept;
Vector3 FromNative(const native::Vector3& value) noexcept;
native::Vector3d ToNative(const Vector3d& value) noexcept;
Vector3d FromNative(const native::Vector3d& value) noexcept;
native::Quaternion ToNative(const Quaternion& value) noexcept;
Quaternion FromNative(const native::Quaternion& value) noexcept;
native::Pose ToNative(const Pose& value) noexcept;
Pose FromNative(const native::Pose& value) noexcept;
native::Fov ToNative(const Fov& value) noexcept;
Fov FromNative(const native::Fov& value) noexcept;
native::TimePoint ToNative(const TimePoint& value) noexcept;
TimePoint FromNative(const native::TimePoint& value) noexcept;
native::Color ToNative(const Color& value) noexcept;
Color FromNative(const native::Color& value) noexcept;
native::GuiPanelPoint ToNative(const GuiPanelPoint& value) noexcept;
GuiPanelPoint FromNative(const native::GuiPanelPoint& value) noexcept;
native::GuiPanelSize ToNative(const GuiPanelSize& value) noexcept;
GuiPanelSize FromNative(const native::GuiPanelSize& value) noexcept;
native::AppVolumeSize ToNative(const AppVolumeSize& value) noexcept;
AppVolumeSize FromNative(const native::AppVolumeSize& value) noexcept;
native::AppVolumeEditingStatus ToNative(const AppVolumeEditingStatus& value) noexcept;
AppVolumeEditingStatus FromNative(const native::AppVolumeEditingStatus& value) noexcept;
native::GeoAnchorFrame ToNative(const GeoAnchorFrame& value) noexcept;
GeoAnchorFrame FromNative(const native::GeoAnchorFrame& value) noexcept;
/// Empty when no anchor is set.
std::optional<GeoAnchorFrame> FromNative(const native::AppWorldAnchor& value) noexcept;
native::GraphicsDeviceId ToNative(const GraphicsDeviceId& value) noexcept;
GraphicsDeviceId FromNative(const native::GraphicsDeviceId& value) noexcept;
native::TextureSize ToNative(const TextureSize& value) noexcept;
TextureSize FromNative(const native::TextureSize& value) noexcept;
native::VideoFrameView ToNative(const VideoFrameView& value) noexcept;
VideoFrameView FromNative(const native::VideoFrameView& value) noexcept;
native::VideoFrameLayout ToNative(const VideoFrameLayout& value) noexcept;
VideoFrameLayout FromNative(const native::VideoFrameLayout& value) noexcept;
VideoFrameCpu FromNative(const native::VideoFrameCpu& value) noexcept;
#ifdef QAR_ENABLE_D3D11
VideoFrameD3D11 FromNative(const native::VideoFrameD3D11& value) noexcept;
#endif
#ifdef QAR_ENABLE_OPENGL
VideoFrameOpenGl FromNative(const native::VideoFrameOpenGl& value) noexcept;
native::OpenGlContextCallbacks ToNative(const OpenGlContextCallbacks& value) noexcept;
#endif
DeviceHands FromNative(const native::DeviceHandsWithJoints& value) noexcept;

} // namespace qaros

#include <qar_streaming_cpp/detail/Values.inl>
