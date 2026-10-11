// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SystemInfo.hpp>

#include <array>
#include <cstddef>
#include <tuple>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace core::platform
{

std::string hostName()
{
#ifdef _WIN32
    // The DNS host name, which is what Qt and a shell's OSC 7 URL call the machine; GetComputerNameA
    // answered the NetBIOS name, upper-cased and cut to 15 characters. The wide call, converted to
    // UTF-8, because the narrow one answers in the machine's ANSI code page (core-cpp#7). The first
    // call reports the size, NUL included; the name is fixed at startup, so it holds for the second.
    auto size = DWORD { 0 };
    std::ignore = GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
    auto wide = std::wstring(size, L'\0');
    if (!GetComputerNameExW(ComputerNameDnsHostname, wide.data(), &size))
        return {};
    auto const wideSize = static_cast<int>(size);
    auto const length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, nullptr, 0, nullptr, nullptr);
    auto name = std::string(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideSize, name.data(), length, nullptr, nullptr);
    return name;
#else
    auto buf = std::array<char, 256> {};
    if (gethostname(buf.data(), buf.size()) == 0)
        return { buf.data() };
    return {};
#endif
}

std::string const& cachedHostName()
{
    static std::string const cached = hostName();
    return cached;
}

} // namespace core::platform
