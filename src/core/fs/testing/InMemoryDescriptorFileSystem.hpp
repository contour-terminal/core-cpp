// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `testing::InMemoryDescriptorFileSystem` — an @c IDescriptorFileSystem held in memory, for tests.

#include <core/async/IExecutor.hpp>
#include <core/async/Task.hpp>
#include <core/fs/Descriptor.hpp>
#include <core/fs/FsError.hpp>
#include <core/fs/FsExport.hpp>
#include <core/fs/PathPolicy.hpp>
#include <core/platform/Clock.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace core::fs::testing
{

/// The nodes and the configuration a file system and its descriptors share; defined in the source.
struct InMemoryState;

/// A file system of nodes in memory: directories, regular files and symbolic links.
///
/// It is held to the same contract as the native one, through the same code where there is code to
/// share: paths are planned by @c fs::detail::planRelativePath and links spliced by
/// @c fs::detail::spliceSymlink under the policy it is given, rights come from @c rightsTable and
/// @c checkOpenRights, and the policy's @c CaseRule decides whether two names are one. **Every
/// operation suspends through @c async::offload**, onto `blocking` and back onto `home`, as the
/// native one does: a model that answered inline could never be in the parked state a cancellation
/// or teardown case is about, and every assertion over it would hold whether the property were
/// implemented or not.
///
/// A write or a size beyond 1 GiB is `FileTooLarge`, so a test can hand it any guest-sized number.
///
/// An unlinked node stays readable through a descriptor that is open on it, and its memory is kept
/// until the file system is destroyed.
///
/// The namespace is `core::fs::testing`, the directory's: inside `namespace core::fs` an unqualified
/// `testing::` names this one, so code there that means the module spells it `core::testing::`.
class CORE_FS_API InMemoryDescriptorFileSystem final: public IDescriptorFileSystem
{
  public:
    /// @param home Where every operation resumes. Must outlive the file system and its descriptors.
    /// @param blocking Where every operation runs. Must outlive them too.
    /// @param clock What `SetToNow` and a new node's times read. Must outlive them too.
    /// @param policy The name rules paths are held to. Must outlive the file system and its
    ///        descriptors: it is kept by address, and its rows are views.
    /// @param maxReadChunk The most one read returns.
    InMemoryDescriptorFileSystem(async::IExecutor& home,
                                 async::IExecutor& blocking,
                                 platform::IWallClock const& clock,
                                 PathPolicy const& policy = posixPathPolicy(),
                                 std::size_t maxReadChunk = std::size_t { 1 } << 20U);

    InMemoryDescriptorFileSystem(InMemoryDescriptorFileSystem const&) = delete;
    InMemoryDescriptorFileSystem(InMemoryDescriptorFileSystem&&) = delete;
    InMemoryDescriptorFileSystem& operator=(InMemoryDescriptorFileSystem const&) = delete;
    InMemoryDescriptorFileSystem& operator=(InMemoryDescriptorFileSystem&&) = delete;
    ~InMemoryDescriptorFileSystem() override;

    /// Creates the directory @p path, and every directory above it, at once (a test's setup).
    /// @param path Relative to the root, `/`-separated.
    /// @throws std::invalid_argument Where something that is not a directory is in the way.
    void addDirectory(std::string_view path);

    /// Creates the regular file @p path holding @p content, and every directory above it.
    /// @param path Relative to the root, `/`-separated.
    /// @param content What it holds.
    /// @throws std::invalid_argument Where something is in the way.
    void addFile(std::string_view path, std::string_view content);

    /// Creates the symbolic link @p path holding @p target, and every directory above it.
    /// @param path Relative to the root, `/`-separated.
    /// @param target What the link names, stored as given.
    /// @throws std::invalid_argument Where something is in the way.
    void addSymlink(std::string_view path, std::string target);

    /// Opens the directory @p hostDirectory of the model: `/`, `.` and the empty path are its root.
    /// @param hostDirectory A `/`-separated path from the root.
    /// @param flags The rights the root carries; `Write` is `IsDirectory`.
    /// @return The descriptor.
    [[nodiscard]] async::Task<FsResult<std::unique_ptr<IDescriptor>>> openPreopen(
        std::filesystem::path hostDirectory, DescriptorFlags flags) override;

  private:
    std::shared_ptr<InMemoryState> _state;
};

} // namespace core::fs::testing
