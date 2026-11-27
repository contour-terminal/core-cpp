// SPDX-License-Identifier: Apache-2.0
//
// Who may reach an object on Linux, and on every POSIX system without an ACL of its own: the owner
// and the mode of an fstat. A POSIX.1e ACL needs no reading of its own here: its named entries are
// bounded by the mask, which the mode's group bits show, so a mode with no group or other bit
// leaves no named entry any right. Nothing else is read, so the facts hold for an fstatat by name
// as for a handle.
#include <core/fs/AccessScope.hpp>
#include <core/fs/posix/OwnerOnlyCreation.hpp>
#include <core/fs/posix/PosixSystem.hpp>

#include <sys/stat.h>

#include <optional>

namespace core::fs::detail
{

std::optional<AccessFacts> accessFactsOf(int /*handle*/, struct ::stat const& status)
{
    return modeFactsOf(status);
}

FsResult<void> stripInheritedAcl(int /*handle*/, ::mode_t /*mode*/)
{
    // A POSIX.1e default ACL replaces the umask step, but each entry it hands down is ANDed with the
    // mode argument's bits: the mask with the group bits, other with the other bits. Under 0600 or
    // 0700 both are zero, so no named entry and no other is left any right; nothing to strip.
    return {};
}

OwnerOnlyCreation nativeOwnerOnlyCreation() noexcept
{
    // The mode caps every entry a default ACL hands down.
    return OwnerOnlyCreation::Direct;
}

} // namespace core::fs::detail
