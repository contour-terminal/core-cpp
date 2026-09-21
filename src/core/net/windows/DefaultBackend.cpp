// SPDX-License-Identifier: Apache-2.0
///
/// Which backends Windows builds: `WSAEventSelect` + `WaitForMultipleObjects`, and —
/// since Task B7a — an I/O completion port. poll(2) is not built here: Winsock's
/// `WSAPoll` is not poll(2), and cannot wait on a console handle or an event, which is
/// most of what a loop on this platform watches. CMake compiles exactly one
/// DefaultBackend.cpp — this one — which is how no `#ifdef` chooses a backend
/// (Ruling R40).
///
/// **The preferred kind is still `Wfmo`, deliberately.** `IocpBackend` is complete and
/// reachable by name, but the sockets that issue overlapped operations on its port are
/// Task B7b's; Windows' sockets today are `WindowsSocket`, which parks on a WSAEVENT.
/// Both backends serve that (a WSAEVENT is a waitable HANDLE and IOCP bridges those), so
/// the parity matrix covers IOCP either way — but making it the DEFAULT before its
/// sockets exist would move every Windows consumer onto a completion port that nothing
/// completes on, for no gain. Task B7b flips this line and keeps Wfmo one release as the
/// fallback ([core-cpp#6](https://github.com/contour-terminal/core-cpp/issues/6)).
#include <core/net/IoBackend.hpp>
#include <core/net/windows/IocpBackend.hpp>
#include <core/net/windows/WfmoBackend.hpp>

#include <memory>

namespace core::net
{

BackendKind preferredBackendKind() noexcept
{
    return BackendKind::Wfmo;
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
    // the same shape as the other four, and so that the fallback is one line when Task
    // B7b makes IOCP the preferred kind.
    if (auto native = makeBackend(preferredBackendKind()))
        return native;
    return std::make_unique<WfmoBackend>();
}

} // namespace core::net
