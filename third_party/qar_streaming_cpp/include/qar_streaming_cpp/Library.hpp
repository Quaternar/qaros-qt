#pragma once

#include <qar_streaming_cpp/Status.hpp>

#include <optional>
#include <string>

namespace qaros
{

/// The options of qar_library_init.
class LibraryBuilder
{
public:
	LibraryBuilder() noexcept;
	LibraryBuilder(const LibraryBuilder&) = delete;
	LibraryBuilder& operator=(const LibraryBuilder&) = delete;

	LibraryBuilder& WithConsoleLogging(bool enabled) noexcept;
	/// UTF-8 path of the folder the library writes its log files to.
	LibraryBuilder& WithLogFolder(std::string path) noexcept;
	LibraryBuilder& WithLogSeverity(LogSeverity severity) noexcept;

	[[nodiscard]]
	const native::LibraryInit& Get() const noexcept;

private:
	void Sync() noexcept;

	native::LibraryInit m_init;
	std::optional<std::string> m_logFolder;
};

/// Owns qar_library_init / qar_library_destroy. Destroy every other handle
/// first. Shutdown() reports the destroy result; the destructor drops it,
/// because no library is left to log it through.
class Library
{
public:
	Library() noexcept = default;

	static Result<Library> Init(const LibraryBuilder& builder = LibraryBuilder()) noexcept;

	Library(Library&& other) noexcept;
	Library& operator=(Library&& other) noexcept;
	Library(const Library&) = delete;
	Library& operator=(const Library&) = delete;
	~Library() noexcept;

	[[nodiscard]]
	Result<void> Shutdown() noexcept;

private:
	bool m_active = false;
};

} // namespace qaros

#include <qar_streaming_cpp/detail/Library.inl>
