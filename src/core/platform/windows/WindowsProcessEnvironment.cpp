// SPDX-License-Identifier: Apache-2.0
#include <core/platform/windows/WindowsProcessEnvironment.hpp>

#include <core/Environment.hpp>

#include <algorithm>
#include <cctype>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <windows.h>

namespace core::platform
{

namespace
{
    /// @return What the process environment's writer answered, as this interface spells it.
    [[nodiscard]] std::expected<void, PlatformError> toPlatformResult(
        std::expected<void, std::error_code> result)
    {
        return result.transform_error([](std::error_code const& error) {
            return error == std::errc::invalid_argument ? PlatformError::InvalidArgument
                                                        : PlatformError::IoError;
        });
    }

    /// @return @p text in UTF-8, as every name core-cpp hands back is spelled.
    [[nodiscard]] std::string toUtf8(std::wstring_view text)
    {
        if (text.empty())
            return {};
        auto const size = static_cast<int>(text.size());
        auto const length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
        auto utf8 = std::string(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), size, utf8.data(), length, nullptr, nullptr);
        return utf8;
    }

    /// @return Whether @p a and @p b name the same variable: ASCII letters compare without case.
    [[nodiscard]] bool sameName(std::string_view a, std::string_view b) noexcept
    {
        return std::ranges::equal(a, b, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        });
    }
} // namespace

bool WindowsProcessEnvironment::CaseInsensitiveLess::operator()(std::string_view a, std::string_view b) const
{
    return std::ranges::lexicographical_compare(
        a, b, [](unsigned char ac, unsigned char bc) { return std::tolower(ac) < std::tolower(bc); });
}

std::unique_ptr<ProcessEnvironment> nativeProcessEnvironment()
{
    return std::make_unique<WindowsProcessEnvironment>();
}

std::expected<void, PlatformError> WindowsProcessEnvironment::set(std::string_view name,
                                                                  std::string_view value)
{
    if (!isValidEnvironmentName(name) || value.contains('\0'))
        return std::unexpected(PlatformError::InvalidArgument);
    _values.insert_or_assign(std::string { name }, std::string { value });
    return {};
}

std::optional<std::string> WindowsProcessEnvironment::get(std::string_view name) const
{
    if (auto const it = _values.find(name); it != _values.end())
        return it->second;

    // core::LiveEnvironment reads the same Win32 block, and reads it right: a return of 0 from
    // GetEnvironmentVariableW is an empty value -- a variable that is set -- unless the call says
    // the name is gone, a value that does not fit the buffer needs the size it reports, and the
    // block is UTF-16, which it converts. The copy that used to stand here got the first two wrong
    // and read through the ANSI code page, so the two readers disagreed about the same block.
    return core::LiveEnvironment {}.get(name);
}

std::expected<void, PlatformError> WindowsProcessEnvironment::unset(std::string_view name)
{
    if (!isValidEnvironmentName(name))
        return std::unexpected(PlatformError::InvalidArgument);
    if (auto const it = _values.find(name); it != _values.end())
        _values.erase(it);
    return toPlatformResult(core::unsetProcessEnvironmentVariable(name));
}

std::expected<void, PlatformError> WindowsProcessEnvironment::exportVariable(std::string_view name)
{
    if (!isValidEnvironmentName(name))
        return std::unexpected(PlatformError::InvalidArgument);
    if (auto const it = _values.find(name); it != _values.end())
        return toPlatformResult(core::setProcessEnvironmentVariable(it->first, it->second));
    return {};
}

std::vector<std::string> WindowsProcessEnvironment::keys() const
{
    std::vector<std::string> result;

    auto const envBlock = GetEnvironmentStringsW();
    if (envBlock != nullptr)
    {
        auto const* p = envBlock;
        while (*p != L'\0')
        {
            auto const entry = std::wstring_view(p);
            // Converted, not narrowed a code unit at a time: a name outside ASCII lost every high
            // byte that way (core-cpp#7).
            if (auto const eq = entry.find(L'='); eq != std::wstring_view::npos && eq > 0)
                result.push_back(toUtf8(entry.substr(0, eq)));
            p += entry.size() + 1;
        }
        FreeEnvironmentStringsW(envBlock);
    }

    for (auto const& [key, _]: _values)
    {
        if (std::ranges::none_of(result,
                                 [&](std::string const& existing) { return sameName(existing, key); }))
            result.push_back(key);
    }

    return result;
}

} // namespace core::platform
