// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The calls in which POSIX systems differ, each defined once per system by a source the build
/// chooses (`posix/PosixSystem.cpp` or `bsd/PosixSystem.cpp`; `linux/Append.cpp` or
/// `posix/Append.cpp`), so the descriptor itself names no system. Private to the fs module.

#include <core/fs/Descriptor.hpp>
#include <core/fs/FsError.hpp>
#include <core/fs/FsExport.hpp>
#include <core/fs/detail/OwnedHandle.hpp>

#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <span>
#include <string>

namespace core::fs::detail
{

/// @param call The system call that failed.
/// @return `errno`, classified, naming @p call.
[[nodiscard]] inline std::unexpected<FsError> errnoError(char const* call)
{
    return std::unexpected { fsErrorFromSystemCode(errno, call) };
}

/// Calls @p call again for as long as a signal interrupts it.
/// @param call A system call that answers -1 and sets `errno` on failure.
/// @return What its last attempt answered.
template <typename F>
[[nodiscard]] auto retryOnInterrupt(F call)
{
    auto result = call();
    while (result == -1 && errno == EINTR)
        result = call();
    return result;
}

/// The three times of a `struct stat`.
struct StatTimes
{
    FileTime accessed;      ///< `st_atim`.
    FileTime modified;      ///< `st_mtim`.
    FileTime statusChanged; ///< `st_ctim`.
};

/// @param status What `fstat` filled in.
/// @return Its times, read through the fields this system names them by.
[[nodiscard]] StatTimes timesOf(struct ::stat const& status) noexcept;

/// Advises the system how a range of @p handle will be read, through this system's call for it.
/// @param handle An open file.
/// @param offset Where the range starts.
/// @param length How long it is; 0 to the end.
/// @param advice How.
/// @return Nothing, or why it was refused.
[[nodiscard]] FsResult<void> adviseRange(int handle,
                                         std::uint64_t offset,
                                         std::uint64_t length,
                                         Advice advice);

/// @param handle An open file.
/// @return Nothing once its data is on stable storage.
[[nodiscard]] FsResult<void> syncData(int handle);

/// @param handle An open file.
/// @return Nothing once its data and metadata are on stable storage.
[[nodiscard]] FsResult<void> syncAll(int handle);

/// @param flags The flags a descriptor is opened with.
/// @return The `open` flags its write-integrity mode needs; `Unsupported` for a mode this system
///         lacks.
[[nodiscard]] FsResult<int> syncOpenFlags(DescriptorFlags flags);

/// Classifies an open of @p name in @p directory that the kernel refused with @p error, where a
/// system's answer differs from Linux's: macOS refuses `O_CREAT` on an existing directory with
/// `EACCES` where Linux says `EISDIR`.
/// @param directory The directory @p name is in.
/// @param name The name the open was given.
/// @param flags The flags it was given.
/// @param error The `errno` it failed with.
/// @return The error.
[[nodiscard]] FsError openFailure(int directory, std::string const& name, int flags, int error);

/// Opens @p name again, waiting, after an `O_NONBLOCK` open of it answered `EWOULDBLOCK`, which
/// only a lease on a regular file makes it answer. Where the system has leases (Linux), the name is
/// opened as a path handle, checked by that handle to be a regular file, and reopened through it
/// with @p flags, waiting for the lease to be broken: whatever was renamed over the name between
/// the two opens, a FIFO is never opened blocking. Elsewhere the answer is `WouldBlock`.
/// @param directory The directory @p name is in.
/// @param name The name.
/// @param flags The open's flags, without `O_NONBLOCK`.
/// @return The handle; `WouldBlock` where @p name no longer names a regular file or the system
///         cannot wait for a lease.
[[nodiscard]] CORE_FS_API FsResult<UniqueHandle> openAfterWouldBlock(int directory,
                                                                     std::string const& name,
                                                                     int flags);

/// Opens the second handle `append` writes through, where this system cannot append through the
/// first without changing how its positioned writes land (an `O_APPEND` handle ignores the offset
/// of `pwrite` on Linux). Never `dup`: a duplicate shares the first handle's status flags.
/// @param directory The directory @p name was opened beneath, still held.
/// @param name The name the first handle was opened by.
/// @param first The first handle; the second must open the same object.
/// @param integrityFlags The write-integrity `open` flags of the first (`O_SYNC`, `O_DSYNC`), so
///        an append is as durable as a positioned write.
/// @return The second handle, or one holding no handle where this system appends through the
///         first; `Busy` where @p name no longer names the object @p first is open on.
[[nodiscard]] FsResult<UniqueHandle> openAppendHandle(int directory,
                                                      std::string const& name,
                                                      int first,
                                                      int integrityFlags);

/// Writes @p bytes at the end of the file.
/// @param handle The descriptor's handle.
/// @param appendHandle What @c openAppendHandle opened for it, or null where it opened none.
/// @param appendLock The descriptor's own, held by an append that has to find the end itself, so
///        two of its appends never land on the same end.
/// @param bytes What to write.
/// @return How many bytes were written.
[[nodiscard]] FsResult<std::size_t> appendBytes(int handle,
                                                OwnedHandle const* appendHandle,
                                                std::mutex& appendLock,
                                                std::span<std::byte const> bytes);

} // namespace core::fs::detail
