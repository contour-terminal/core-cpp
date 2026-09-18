// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32
    #include <windows.h>
#elifdef __EMSCRIPTEN__
// Single-threaded WebAssembly: one thread, and no name to give it.
#elifdef __OpenBSD__
    #include <pthread_np.h>
    #define pthread_getname_np pthread_get_name_np
#else
    #include <pthread.h>
#endif

#include <core/Utils.hpp>

namespace core
{

using namespace std::string_literals;

std::string threadName()
{
#ifdef _WIN32
    auto const threadHandle = GetCurrentThread();
    PWSTR pwsz = nullptr;
    HRESULT hr = GetThreadDescription(threadHandle, &pwsz);
    if (SUCCEEDED(hr))
    {
        int const len = WideCharToMultiByte(CP_UTF8, 0, pwsz, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Str(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, pwsz, -1, utf8Str.data(), len, nullptr, nullptr);
        utf8Str.resize(static_cast<std::size_t>(len - 1));
        LocalFree(pwsz);
        return utf8Str;
    }
    return ""s;
#elifdef __EMSCRIPTEN__
    // Emscripten's libc leaves pthread_getname_np() out, and without pthreads there is nothing
    // to name.
    return ""s;
#else
    char text[32] = {};
    pthread_getname_np(pthread_self(), text, sizeof(text));
    return text;
#endif
}

} // namespace core
