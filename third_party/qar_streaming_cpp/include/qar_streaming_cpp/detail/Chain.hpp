#pragma once

// Links a C init and its extensions through `header.next`. Internal to this
// library; the builders call it after every change.

#include <qar_streaming_cpp/Types.hpp>

#include <initializer_list>
#include <optional>
#include <string>

namespace qaros::detail
{

/// `head -> extensions... -> null`, skipping null extensions.
inline void
Link(
	native::StructureHeader* head, std::initializer_list<native::StructureHeader*> extensions
) noexcept
{
	native::StructureHeader* tail = head;
	tail->next = nullptr;
	for(native::StructureHeader* extension : extensions)
	{
		if(extension == nullptr)
		{
			continue;
		}
		tail->next = extension;
		tail = extension;
		tail->next = nullptr;
	}
}

/// The header of an extension a builder may chain, or null.
template<typename T>
native::StructureHeader*
OptionalHeader(std::optional<T>& extension) noexcept
{
	return extension.has_value() ? &extension->header : nullptr;
}

/// The text a builder owns, as the C API wants it: null when unset.
inline const char*
OptionalText(const std::optional<std::string>& text) noexcept
{
	return text.has_value() ? text->c_str() : nullptr;
}

} // namespace qaros::detail
