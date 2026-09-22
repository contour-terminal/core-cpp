// SPDX-License-Identifier: Apache-2.0
///
/// Which backends Windows builds: `WSAEventSelect` + `WaitForMultipleObjects`, and —
/// since Task B7a — an I/O completion port. poll(2) is not built here: Winsock's
/// `WSAPoll` is not poll(2), and cannot wait on a console handle or an event, which is
/// most of what a loop on this platform watches. CMake compiles exactly one
/// DefaultBackend.cpp — this one — which is how no `#ifdef` chooses a backend
/// (Ruling R40).
///
/// **The preferred kind is `Iocp`, since Task B7b.** Task B7a built the port and kept it off
/// the default, because the sockets that issue overlapped operations on it did not exist yet;
/// `IocpSocket`, `IocpListener` and the `ConnectEx` dial are those sockets, and every factory asks
/// the loop which one to build (`EventLoop::completionPort`). `Wfmo` stays reachable by name for
/// one release as the fallback — `makeBackend(BackendKind::Wfmo)` for a caller that wants it
/// explicitly, and the fallback below when the port cannot be created — and is then removed
/// ([core-cpp#6](https://github.com/contour-terminal/core-cpp/issues/6)).
#include <core/net/Diagnostics.hpp>
#include <core/net/IoBackend.hpp>
#include <core/net/windows/IocpBackend.hpp>
#include <core/net/windows/WfmoBackend.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace core::net
{

BackendKind preferredBackendKind() noexcept
{
    return BackendKind::Iocp;
}

std::unique_ptr<IoBackend> makeBackend(BackendKind kind)
{
    switch (kind)
    {
        case BackendKind::Iocp: return std::make_unique<IocpBackend>();
        case BackendKind::Wfmo: return std::make_unique<WfmoBackend>();

        // Not built here: poll(2), epoll and kqueue are the POSIX platforms'.
        case BackendKind::Poll:
        case BackendKind::Epoll:
        case BackendKind::Kqueue: return nullptr;

        // Reachable everywhere, and not through here — see posix/DefaultBackend.cpp.
        case BackendKind::HostDriven:
        case BackendKind::Scripted:
        case BackendKind::Null: return nullptr;

        case BackendKind::Last: break;
    }
    return nullptr;
}

std::unique_ptr<IoBackend> makeDefaultBackend()
{
    // Through makeBackend rather than straight to the constructor, so that this file is
    // the same shape as the other four. The fallback is WFMO, and the case it covers is
    // narrow: `IocpBackend` throws only when the port itself cannot be created, which is
    // handle exhaustion -- and a WFMO backend is then the one that can still be built.
    try
    {
        if (auto native = makeBackend(preferredBackendKind()))
            return native;
    }
    catch (std::runtime_error const& refusal)
    {
        // Said, then fallen through to the backend that needs no port: silent to the caller, who
        // has nothing to decide (every backend behaves alike), and not silent to whoever reads the
        // diagnostics of a process that is running short of handles. If handles are exhausted,
        // WFMO's own constructor throws the same kind of error, and that one propagates.
        reportDiagnostic(std::string { "core::net: the IOCP backend could not be created, so the default "
                                       "is WFMO: " }
                         + refusal.what());
    }
    return std::make_unique<WfmoBackend>();
}

} // namespace core::net
