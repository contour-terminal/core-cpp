// SPDX-License-Identifier: Apache-2.0
#include <core/platform/windows/WindowsEnvironmentProvider.hpp>

#ifdef _WIN32
    #include <core/Environment.hpp>
    #include <core/platform/PathUtils.hpp>

    #include <algorithm>
    #include <cctype>
    #include <filesystem>
    #include <memory>
    #include <tuple>

    #include <windows.h>

namespace core::platform
{

bool WindowsEnvironmentProvider::CaseInsensitiveLess::operator()(std::string const& a,
                                                                 std::string const& b) const
{
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(), [](unsigned char ac, unsigned char bc) {
            return std::tolower(ac) < std::tolower(bc);
        });
}

WindowsEnvironmentProvider& WindowsEnvironmentProvider::instance()
{
    static WindowsEnvironmentProvider provider;
    return provider;
}

std::unique_ptr<EnvironmentProvider> nativeEnvironmentProvider()
{
    return std::make_unique<WindowsEnvironmentProvider>();
}

void WindowsEnvironmentProvider::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string> WindowsEnvironmentProvider::get(std::string_view name) const
{
    if (auto const it = _values.find(std::string(name)); it != _values.end())
        return it->second;

    // core::LiveEnvironment reads the same Win32 block, and reads it right: a return of 0 from
    // GetEnvironmentVariableA is an empty value -- a variable that is set -- unless the call says
    // the name is gone, and a value that does not fit the buffer needs the size it reports. The
    // copy that used to stand here got both wrong, so the two readers disagreed about the same
    // block; it also allocated 32 KiB on every lookup, where this sizes the buffer to the value.
    return core::LiveEnvironment {}.get(name);
}

void WindowsEnvironmentProvider::unset(std::string_view name)
{
    _values.erase(std::string(name));
    // The interface has no error channel; a name the environment cannot hold is not set there.
    std::ignore = core::unsetProcessEnvironmentVariable(name);
}

void WindowsEnvironmentProvider::exportVariable(std::string_view name)
{
    if (auto const it = _values.find(std::string(name)); it != _values.end())
        // The interface has no error channel; a name the environment cannot hold stays local.
        std::ignore = core::setProcessEnvironmentVariable(it->first, it->second);
}

std::vector<std::string> WindowsEnvironmentProvider::keys() const
{
    std::vector<std::string> result;

    auto const envBlock = GetEnvironmentStringsW();
    if (envBlock != nullptr)
    {
        auto const* p = envBlock;
        while (*p != L'\0')
        {
            auto const entry = std::wstring_view(p);
            auto const eq = entry.find(L'=');
            if (eq != std::wstring_view::npos && eq > 0)
            {
                auto const wideKey = entry.substr(0, eq);
                std::string narrowKey;
                narrowKey.reserve(wideKey.size());
                for (auto const wc: wideKey)
                    narrowKey += static_cast<char>(wc);
                result.push_back(std::move(narrowKey));
            }
            p += entry.size() + 1;
        }
        FreeEnvironmentStringsW(envBlock);
    }

    for (auto const& [key, _]: _values)
    {
        auto const found = std::find_if(result.begin(), result.end(), [&](std::string const& existing) {
            return std::equal(existing.begin(), existing.end(), key.begin(), key.end(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a))
                       == std::tolower(static_cast<unsigned char>(b));
            });
        });
        if (found == result.end())
            result.push_back(key);
    }

    return result;
}

std::expected<void, PlatformError> WindowsEnvironmentProvider::changeDirectory(
    std::filesystem::path const& path)
{
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    if (ec)
        return std::unexpected(PlatformError::IoError);
    return {};
}

std::string WindowsEnvironmentProvider::currentDirectory() const
{
    // Report the real on-disk capitalization (and an upper-case drive letter) so that
    // PWD, and whatever shows the user the directory, agree with how it is actually stored,
    // rather than echoing whatever case was passed to SetCurrentDirectory.
    return canonicalCasePath(std::filesystem::current_path());
}

} // namespace core::platform

#endif // _WIN32
