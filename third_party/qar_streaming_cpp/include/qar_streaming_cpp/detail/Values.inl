#pragma once

// Implementation of Values.hpp.

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace qaros
{

// NativeOf: the C struct behind each value type

#define QAROS_NATIVE_OF(CPP_TYPE, NATIVE_TYPE)                                                     \
	template<>                                                                                     \
	struct NativeOf<CPP_TYPE>                                                                      \
	{                                                                                              \
		using Type = NATIVE_TYPE;                                                                  \
	};

QAROS_NATIVE_OF(AppVolumeId, native::AppVolumeId)
QAROS_NATIVE_OF(GuiPanelId, native::GuiPanelId)
QAROS_NATIVE_OF(OnboardingId, native::OnboardingId)
QAROS_NATIVE_OF(PeerId, native::PeerId)
QAROS_NATIVE_OF(SessionId, native::SessionId)
QAROS_NATIVE_OF(StreamId, native::StreamId)
QAROS_NATIVE_OF(Vector3, native::Vector3)
QAROS_NATIVE_OF(Vector3d, native::Vector3d)
QAROS_NATIVE_OF(Quaternion, native::Quaternion)
QAROS_NATIVE_OF(Pose, native::Pose)
QAROS_NATIVE_OF(Fov, native::Fov)
QAROS_NATIVE_OF(TimePoint, native::TimePoint)
QAROS_NATIVE_OF(Color, native::Color)
QAROS_NATIVE_OF(GuiPanelPoint, native::GuiPanelPoint)
QAROS_NATIVE_OF(GuiPanelSize, native::GuiPanelSize)
QAROS_NATIVE_OF(AppVolumeSize, native::AppVolumeSize)
QAROS_NATIVE_OF(AppVolumeEditingStatus, native::AppVolumeEditingStatus)
QAROS_NATIVE_OF(GeoAnchorFrame, native::GeoAnchorFrame)
QAROS_NATIVE_OF(GraphicsDeviceId, native::GraphicsDeviceId)
QAROS_NATIVE_OF(TextureSize, native::TextureSize)
QAROS_NATIVE_OF(VideoFrameView, native::VideoFrameView)
QAROS_NATIVE_OF(VideoFrameLayout, native::VideoFrameLayout)

#undef QAROS_NATIVE_OF

// Id

template<typename Tag>
bool
Id<Tag>::IsZero() const noexcept
{
	const auto is_zero = [](uint8_t byte) noexcept
	{
		return byte == 0;
	};
	return std::all_of(Bytes.begin(), Bytes.end(), is_zero);
}

template<typename Tag>
Result<std::string>
Id<Tag>::ToString() const noexcept
{
	std::array<char, QAR_UUID_TEXT_BUFFER_SIZE> text = {};
	const Status status(qar_uuid_to_string(Bytes.data(), text.data(), text.size()));
	if(not status.Ok())
	{
		return Result<std::string>(status);
	}
	return std::string(text.data());
}

template<typename Tag>
Result<Id<Tag>>
Id<Tag>::FromString(const std::string& text) noexcept
{
	Id id;
	const Status status(qar_uuid_from_string(text.c_str(), id.Bytes.data()));
	if(not status.Ok())
	{
		return Result<Id>(status);
	}
	return id;
}

template<typename Tag>
bool
operator==(const Id<Tag>& left, const Id<Tag>& right) noexcept
{
	return left.Bytes == right.Bytes;
}

template<typename Tag>
bool
operator!=(const Id<Tag>& left, const Id<Tag>& right) noexcept
{
	return left.Bytes != right.Bytes;
}

template<typename Tag>
bool
operator<(const Id<Tag>& left, const Id<Tag>& right) noexcept
{
	return left.Bytes < right.Bytes;
}

template<typename Tag>
typename NativeOf<Id<Tag>>::Type
ToNative(const Id<Tag>& id) noexcept
{
	typename NativeOf<Id<Tag>>::Type native = {};
	std::memcpy(native.data, id.Bytes.data(), sizeof(native.data));
	return native;
}

namespace detail
{

template<typename Cpp, typename Native>
Cpp
IdFromNative(const Native& native) noexcept
{
	static_assert(sizeof(native.data) == std::tuple_size_v<decltype(Cpp::Bytes)>);
	Cpp id;
	std::memcpy(id.Bytes.data(), native.data, sizeof(native.data));
	return id;
}

template<typename Cpp, typename Native, size_t Capacity>
std::vector<Cpp>
FromNativeArray(const Native (&values)[Capacity], size_t count) noexcept
{
	std::vector<Cpp> converted;
	const size_t valid = std::min(count, Capacity);
	converted.reserve(valid);
	for(size_t index = 0; index < valid; ++index)
	{
		converted.push_back(FromNative(values[index]));
	}
	return converted;
}

template<typename Cpp, typename Native, size_t Capacity>
size_t
ToNativeArray(const std::vector<Cpp>& values, Native (&out)[Capacity]) noexcept
{
	const size_t count = std::min(values.size(), Capacity);
	for(size_t index = 0; index < count; ++index)
	{
		out[index] = ToNative(values[index]);
	}
	return count;
}

} // namespace detail

inline AppVolumeId
FromNative(const native::AppVolumeId& id) noexcept
{
	return detail::IdFromNative<AppVolumeId>(id);
}

inline GuiPanelId
FromNative(const native::GuiPanelId& id) noexcept
{
	return detail::IdFromNative<GuiPanelId>(id);
}

inline OnboardingId
FromNative(const native::OnboardingId& id) noexcept
{
	return detail::IdFromNative<OnboardingId>(id);
}

inline PeerId
FromNative(const native::PeerId& id) noexcept
{
	return detail::IdFromNative<PeerId>(id);
}

inline SessionId
FromNative(const native::SessionId& id) noexcept
{
	return detail::IdFromNative<SessionId>(id);
}

inline StreamId
FromNative(const native::StreamId& id) noexcept
{
	return detail::IdFromNative<StreamId>(id);
}

// Math and time

inline native::Vector3
ToNative(const Vector3& value) noexcept
{
	return native::Vector3{ value.X, value.Y, value.Z };
}

inline Vector3
FromNative(const native::Vector3& value) noexcept
{
	return Vector3{ value.x, value.y, value.z };
}

inline native::Vector3d
ToNative(const Vector3d& value) noexcept
{
	return native::Vector3d{ value.X, value.Y, value.Z };
}

inline Vector3d
FromNative(const native::Vector3d& value) noexcept
{
	return Vector3d{ value.x, value.y, value.z };
}

inline native::Quaternion
ToNative(const Quaternion& value) noexcept
{
	return native::Quaternion{ value.X, value.Y, value.Z, value.W };
}

inline Quaternion
FromNative(const native::Quaternion& value) noexcept
{
	return Quaternion{ value.x, value.y, value.z, value.w };
}

inline native::Pose
ToNative(const Pose& value) noexcept
{
	return native::Pose{ ToNative(value.Orientation), ToNative(value.Position) };
}

inline Pose
FromNative(const native::Pose& value) noexcept
{
	return Pose{ FromNative(value.orientation), FromNative(value.position) };
}

inline native::Fov
ToNative(const Fov& value) noexcept
{
	return native::Fov{ value.AngleLeft, value.AngleRight, value.AngleUp, value.AngleDown };
}

inline Fov
FromNative(const native::Fov& value) noexcept
{
	return Fov{ value.angle_left, value.angle_right, value.angle_up, value.angle_down };
}

inline native::TimePoint
ToNative(const TimePoint& value) noexcept
{
	return native::TimePoint{ static_cast<int64_t>(value.SinceUnixEpoch.count()) };
}

inline TimePoint
FromNative(const native::TimePoint& value) noexcept
{
	return TimePoint{ std::chrono::nanoseconds(value.unix_ns) };
}

inline native::Color
ToNative(const Color& value) noexcept
{
	return native::Color{ value.R, value.G, value.B, value.A };
}

inline Color
FromNative(const native::Color& value) noexcept
{
	return Color{ value.r, value.g, value.b, value.a };
}

// Panels and volumes

inline native::GuiPanelPoint
ToNative(const GuiPanelPoint& value) noexcept
{
	return native::GuiPanelPoint{ value.XPixels, value.YPixels };
}

inline GuiPanelPoint
FromNative(const native::GuiPanelPoint& value) noexcept
{
	return GuiPanelPoint{ value.x_pixels, value.y_pixels };
}

inline native::GuiPanelSize
ToNative(const GuiPanelSize& value) noexcept
{
	return native::GuiPanelSize{ value.WidthMeters, value.HeightMeters, value.ContentScale };
}

inline GuiPanelSize
FromNative(const native::GuiPanelSize& value) noexcept
{
	return GuiPanelSize{ value.width_meters, value.height_meters, value.content_scale };
}

inline native::AppVolumeSize
ToNative(const AppVolumeSize& value) noexcept
{
	return native::AppVolumeSize{ value.WidthMeters, value.LengthMeters, value.HeightMeters };
}

inline AppVolumeSize
FromNative(const native::AppVolumeSize& value) noexcept
{
	return AppVolumeSize{ value.width_meters, value.length_meters, value.height_meters };
}

inline native::AppVolumeEditingStatus
ToNative(const AppVolumeEditingStatus& value) noexcept
{
	return native::AppVolumeEditingStatus{ value.IsBeingEdited, ToNative(value.Editor) };
}

inline AppVolumeEditingStatus
FromNative(const native::AppVolumeEditingStatus& value) noexcept
{
	return AppVolumeEditingStatus{ value.is_being_edited, FromNative(value.editor_peer) };
}

inline native::GeoAnchorFrame
ToNative(const GeoAnchorFrame& value) noexcept
{
	native::GeoAnchorFrame frame = {};
	frame.world_ref_system = ToNative(value.ReferenceSystem);
	frame.origin_world = ToNative(value.Origin);
	frame.axis_x_world = ToNative(value.AxisX);
	frame.axis_y_world = ToNative(value.AxisY);
	frame.axis_z_world = ToNative(value.AxisZ);
	frame.handedness = ToNative(value.Handedness);
	return frame;
}

inline GeoAnchorFrame
FromNative(const native::GeoAnchorFrame& value) noexcept
{
	GeoAnchorFrame frame;
	frame.ReferenceSystem = FromNative(value.world_ref_system);
	frame.Origin = FromNative(value.origin_world);
	frame.AxisX = FromNative(value.axis_x_world);
	frame.AxisY = FromNative(value.axis_y_world);
	frame.AxisZ = FromNative(value.axis_z_world);
	frame.Handedness = FromNative(value.handedness);
	return frame;
}

inline std::optional<GeoAnchorFrame>
FromNative(const native::AppWorldAnchor& value) noexcept
{
	if(not value.has_anchor)
	{
		return std::nullopt;
	}
	return FromNative(value.anchor);
}

inline native::GraphicsDeviceId
ToNative(const GraphicsDeviceId& value) noexcept
{
	static_assert(sizeof(QarLuid) == std::tuple_size_v<decltype(GraphicsDeviceId::Luid)>);
	native::GraphicsDeviceId device = native::Default<native::GraphicsDeviceId>();
	device.id_type = ToNative(value.Type);
	std::memcpy(&device.luid, value.Luid.data(), value.Luid.size());
	std::memcpy(device.uuid, value.Uuid.data(), value.Uuid.size());
	return device;
}

inline GraphicsDeviceId
FromNative(const native::GraphicsDeviceId& value) noexcept
{
	GraphicsDeviceId device;
	device.Type = FromNative(value.id_type);
	std::memcpy(device.Luid.data(), &value.luid, device.Luid.size());
	std::memcpy(device.Uuid.data(), value.uuid, device.Uuid.size());
	return device;
}

// Frames

inline native::TextureSize
ToNative(const TextureSize& value) noexcept
{
	return native::TextureSize{
		ToNative(value.Format), value.Width, value.Height, value.ArrayLayers
	};
}

inline TextureSize
FromNative(const native::TextureSize& value) noexcept
{
	return TextureSize{ FromNative(value.format), value.width, value.height, value.array_layers };
}

inline native::VideoFrameView
ToNative(const VideoFrameView& value) noexcept
{
	native::VideoFrameView view = {};
	view.start_x = value.StartX;
	view.start_y = value.StartY;
	view.end_x = value.EndX;
	view.end_y = value.EndY;
	view.texture_index = value.TextureIndex;
	view.array_layer_index = value.ArrayLayerIndex;
	view.texture_format = ToNative(value.TextureFormat);
	view.data_type = ToNative(value.Type);
	view.eye = ToNative(value.Eye);
	return view;
}

inline VideoFrameView
FromNative(const native::VideoFrameView& value) noexcept
{
	VideoFrameView view;
	view.StartX = value.start_x;
	view.StartY = value.start_y;
	view.EndX = value.end_x;
	view.EndY = value.end_y;
	view.TextureIndex = value.texture_index;
	view.ArrayLayerIndex = value.array_layer_index;
	view.TextureFormat = FromNative(value.texture_format);
	view.Type = FromNative(value.data_type);
	view.Eye = FromNative(value.eye);
	return view;
}

inline Result<void>
VideoFrameLayout::Validate() const noexcept
{
	if(Views.size() > MaxFrameViews or Textures.size() > MaxFrameTextures)
	{
		return Result<void>(Status::Error(
			StatusCode::ArgumentNotSupported,
			"frame layout has more views than MaxFrameViews or textures than MaxFrameTextures"
		));
	}
	return Result<void>();
}

inline native::VideoFrameLayout
ToNative(const VideoFrameLayout& value) noexcept
{
	native::VideoFrameLayout layout = native::Default<native::VideoFrameLayout>();
	layout.views_count = detail::ToNativeArray(value.Views, layout.views);
	layout.textures_count = detail::ToNativeArray(value.Textures, layout.textures);
	return layout;
}

inline VideoFrameLayout
FromNative(const native::VideoFrameLayout& value) noexcept
{
	VideoFrameLayout layout;
	layout.Views = detail::FromNativeArray<VideoFrameView>(value.views, value.views_count);
	layout.Textures = detail::FromNativeArray<TextureSize>(value.textures, value.textures_count);
	return layout;
}

namespace detail
{

inline VideoTextureCpu
TextureFromNative(const native::VideoTextureCpu& value) noexcept
{
	return VideoTextureCpu{
		FromNative(value.size), value.pitch, value.texture_data, value.texture_data_size
	};
}

} // namespace detail

inline VideoFrameCpu
FromNative(const native::VideoFrameCpu& value) noexcept
{
	VideoFrameCpu frame;
	frame.TextureViews =
		detail::FromNativeArray<VideoFrameView>(value.texture_views, value.texture_views_count);
	const size_t count = std::min(value.textures_count, MaxFrameTextures);
	for(size_t index = 0; index < count; ++index)
	{
		frame.Textures.push_back(detail::TextureFromNative(value.textures[index]));
	}
	return frame;
}

#ifdef QAR_ENABLE_D3D11
inline VideoFrameD3D11
FromNative(const native::VideoFrameD3D11& value) noexcept
{
	VideoFrameD3D11 frame;
	frame.TextureViews =
		detail::FromNativeArray<VideoFrameView>(value.texture_views, value.texture_views_count);
	const size_t count = std::min(value.textures_count, MaxFrameTextures);
	for(size_t index = 0; index < count; ++index)
	{
		const QarVideoTextureD3D11& texture = value.textures[index];
		frame.Textures.push_back(VideoTextureD3D11{ FromNative(texture.size), texture.texture });
	}
	const size_t sync_count = std::min(value.synchronization.texture_sync_count, MaxFrameTextures);
	for(size_t index = 0; index < sync_count; ++index)
	{
		const QarDXGIKeyedMutexSync& sync = value.synchronization.texture_sync[index];
		frame.TextureSync.push_back(KeyedMutexSync{ sync.mutex, sync.acquire, sync.release });
	}
	return frame;
}
#endif

#ifdef QAR_ENABLE_OPENGL
inline VideoFrameOpenGl
FromNative(const native::VideoFrameOpenGl& value) noexcept
{
	VideoFrameOpenGl frame;
	frame.TextureViews =
		detail::FromNativeArray<VideoFrameView>(value.texture_views, value.texture_views_count);
	const size_t count = std::min(value.textures_count, MaxFrameTextures);
	for(size_t index = 0; index < count; ++index)
	{
		const QarVideoTextureOpenGl& texture = value.textures[index];
		frame.Textures.push_back(VideoTextureOpenGl{ FromNative(texture.size), texture.texture });
	}
	return frame;
}

inline native::OpenGlContextCallbacks
ToNative(const OpenGlContextCallbacks& value) noexcept
{
	native::OpenGlContextCallbacks callbacks = {};
	callbacks.user_state = value.UserState;
	callbacks.get_proc_address = value.GetProcAddress;
	callbacks.make_current = value.MakeCurrent;
	callbacks.capture_current = value.CaptureCurrent;
	callbacks.restore_current = value.RestoreCurrent;
	return callbacks;
}
#endif

#ifdef QAR_ENABLE_VULKAN
inline VulkanFrame::VulkanFrame(native::VideoFrameVulkan* frame) noexcept
	: m_frame(frame)
{
}

inline std::vector<VideoFrameView>
VulkanFrame::TextureViews() const noexcept
{
	return detail::FromNativeArray<VideoFrameView>(
		m_frame->texture_views, m_frame->texture_views_count
	);
}

inline size_t
VulkanFrame::TextureCount() const noexcept
{
	return std::min(m_frame->textures_count, MaxFrameTextures);
}

inline VideoTextureVulkan
VulkanFrame::Texture(size_t index) const noexcept
{
	const QarVideoTextureVulkan& texture = m_frame->textures[index];
	VideoTextureVulkan value;
	value.Size = FromNative(texture.size);
	value.Image = texture.image;
	value.Memory = texture.image_memory;
	value.FormatFeatures = texture.format_features;
	value.Layout = texture.layout;
	value.QueueFamilyIndex = texture.queue_family_index;
	return value;
}

inline void
VulkanFrame::SetTextureState(
	size_t index, VkImageLayout layout, uint32_t queue_family_index
) noexcept
{
	m_frame->textures[index].layout = layout;
	m_frame->textures[index].queue_family_index = queue_family_index;
}

inline VkSemaphore
VulkanFrame::Semaphore() const noexcept
{
	return m_frame->synchronization.semaphore;
}

inline VkFence
VulkanFrame::Fence() const noexcept
{
	return m_frame->synchronization.fence;
}

inline native::VideoFrameVulkan*
VulkanFrame::Native() const noexcept
{
	return m_frame;
}
#endif

// Hands

namespace detail
{

inline HandJoints
HandFromNative(const native::HandJoints& value) noexcept
{
	HandJoints hand;
	hand.IsTracked = value.is_tracked;
	hand.IsActive = value.is_active;
	hand.Pose = FromNative(value.pose);
	hand.HasVelocity = value.has_velocity;
	for(size_t index = 0; index < HandJointCount; ++index)
	{
		const native::HandJointLocation& location = value.joint_locations[index];
		hand.JointLocations[index] = HandJointLocation{
			static_cast<HandJoint>(location.joint_id),
			location.location_flags,
			FromNative(location.pose),
			location.radius,
		};
		const native::HandJointVelocity& velocity = value.joint_velocities[index];
		hand.JointVelocities[index] = HandJointVelocity{
			static_cast<HandJoint>(velocity.joint_id),
			velocity.flags,
			FromNative(velocity.linear_velocity),
			FromNative(velocity.angular_velocity),
		};
	}
	return hand;
}

} // namespace detail

inline DeviceHands
FromNative(const native::DeviceHandsWithJoints& value) noexcept
{
	DeviceHands hands;
	hands.Device = FromNative(value.device_id);
	hands.DeviceTimestamp = FromNative(value.device_timestamp);
	hands.Left = detail::HandFromNative(value.left_hand);
	hands.Right = detail::HandFromNative(value.right_hand);
	return hands;
}

} // namespace qaros

namespace std
{

template<typename Tag>
struct hash<qaros::Id<Tag>>
{
	size_t operator()(const qaros::Id<Tag>& id) const noexcept
	{
		size_t seed = 0;
		for(uint8_t byte : id.Bytes)
		{
			seed = seed * 131 + byte;
		}
		return seed;
	}
};

} // namespace std
