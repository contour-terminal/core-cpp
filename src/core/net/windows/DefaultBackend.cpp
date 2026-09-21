// SPDX-License-Identifier: Apache-2.0
///
/// Which backend Windows builds: `WSAEventSelect` + `WaitForMultipleObjects`, and
/// nothing else yet. Task B7 adds IOCP and makes it the default, keeping Wfmo as its
/// fallback for one release. poll(2) is not built here: Winsock's `WSAPoll` is not
/// poll(2) — it cannot wait on a console handle or an event, which is most of what a
/// loop on this platform watches. CMake compiles exactly one DefaultBackend.cpp —
/// this one here — which is how no `#ifdef` chooses a backend (Ruling R40).
#include <core/net/IoBackend.hpp>
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
        case BackendKind::Wfmo: return std::make_unique<WfmoBackend>();

        // Not built here. Iocp arrives in Task B7; poll(2), epoll and kqueue are the
        // POSIX platforms'.
        case BackendKind::Iocp:
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
    // the same shape as the other four. It matters most here: Task B7 adds IOCP beside
    // Wfmo and makes it the preferred kind, and the difference between the two shapes is
    // precisely the `good()` check and the fallback that a second backend needs.
    if (auto native = makeBackend(preferredBackendKind()))
        return native;
    return std::make_unique<WfmoBackend>();
}

} // namespace core::net
