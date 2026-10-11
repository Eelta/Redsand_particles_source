#pragma once

#include <filesystem>
#include <array>
#include <string_view>
#include <utility>

#ifndef REDSAND_LOG_STANDALONE
#include <fmt/format.h>
#endif

namespace RedsandLog
{
	enum class Level { Info, Warning };
	inline constexpr std::size_t maxFileBytes = 8 * 1024 * 1024;
	inline constexpr std::size_t maxMessageBytes = 16 * 1024;

	void Initialize(const std::filesystem::path& directory = {}) noexcept;
	[[nodiscard]] bool Enabled() noexcept;
	void Sync() noexcept;
	void Write(Level level, std::string_view message) noexcept;

#ifndef REDSAND_LOG_STANDALONE
	template <class... Args>
	void FormatWrite(Level level, fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		if (!Enabled()) { return; }
		try {
			std::array<char, maxMessageBytes + 1> buffer;
			const auto result = fmt::format_to_n(buffer.data(), buffer.size(), format, std::forward<Args>(args)...);
			Write(level, { buffer.data(), result.size < buffer.size() ? result.size : buffer.size() });
		} catch (...) {}
	}

	template <class... Args>
	void info(fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		FormatWrite(Level::Info, format, std::forward<Args>(args)...);
	}

	template <class... Args>
	void warn(fmt::format_string<Args...> format, Args&&... args) noexcept
	{
		FormatWrite(Level::Warning, format, std::forward<Args>(args)...);
	}
#endif
}
