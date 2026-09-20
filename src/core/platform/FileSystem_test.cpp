// SPDX-License-Identifier: Apache-2.0
#include <core/platform/NativeFileSystem.hpp>
#include <core/platform/testing/InMemoryFileSystem.hpp>
#include <core/testing/ScopedTempDir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using core::platform::FileSystem;
using core::platform::testing::InMemoryFileSystem;

namespace
{
/// Builds a small tree used by the walk tests.
InMemoryFileSystem makeTree()
{
    return InMemoryFileSystem {
        { .path = "/root", .isDirectory = true },
        { .path = "/root/a", .isDirectory = true },
        { .path = "/root/a/file1.txt", .content = "one" },
        { .path = "/root/a/file2.txt", .content = "two" },
        { .path = "/root/b", .isDirectory = true },
        { .path = "/root/b/file3.txt", .content = "three" },
    };
}

/// @return What @p path holds, or the reason it could not be read (so a mismatch says which).
[[nodiscard]] std::string contentsOf(FileSystem const& fs, std::filesystem::path const& path)
{
    auto const read = fs.readFile(path);
    return read.has_value() ? *read : "<unreadable: " + read.error() + ">";
}

/// What one backend's write streams leave a file holding.
struct StreamOutcome
{
    std::string afterTruncatingWrite;
    std::string afterAppend;
    std::string afterOverwriteInPlace;
    std::string readAfterOverwrite;

    bool operator==(StreamOutcome const&) const = default;
};

/// Runs one script of stream opens over @p fs, so the model and the real filesystem can be held
/// to the same answers: code written against one is tested against the other.
[[nodiscard]] StreamOutcome runStreamScript(FileSystem const& fs, std::filesystem::path const& path)
{
    auto outcome = StreamOutcome {};

    // Truncate discards what the file held.
    REQUIRE(fs.writeFile(path, "seed").has_value());
    {
        auto stream = fs.openWrite(path, core::platform::WriteMode::Truncate);
        REQUIRE(stream.has_value());
        **stream << "0123456789";
    }
    outcome.afterTruncatingWrite = contentsOf(fs, path);

    // Append keeps it and writes after it.
    {
        auto stream = fs.openWrite(path, core::platform::WriteMode::Append);
        REQUIRE(stream.has_value());
        **stream << "ABC";
    }
    outcome.afterAppend = contentsOf(fs, path);

    // A read-write stream starts at the beginning of the file and overwrites from there; it
    // extends the file only past its end, and never appends.
    {
        auto stream = fs.openReadWrite(path);
        REQUIRE(stream.has_value());
        **stream << "xy";
        (*stream)->seekg(4);
        auto tail = std::array<char, 4> {};
        (*stream)->read(tail.data(), static_cast<std::streamsize>(tail.size()));
        outcome.readAfterOverwrite.assign(tail.data(), static_cast<std::size_t>((*stream)->gcount()));
    }
    outcome.afterOverwriteInPlace = contentsOf(fs, path);

    return outcome;
}
} // namespace

TEST_CASE("walkDirectoryRecursive yields every entry exactly once", "[FileSystem]")
{
    auto const fs = makeTree();

    auto visited = std::vector<std::string> {};
    for (auto const& entry: fs.walkDirectoryRecursive("/root"))
        visited.push_back(entry.path.generic_string());

    // The streaming walk must yield the same set of entries the materializing
    // listDirectoryRecursive returns — it is the single source of truth.
    auto const listed = fs.listDirectoryRecursive("/root");
    REQUIRE(listed.has_value());
    REQUIRE(visited.size() == listed->size());

    std::ranges::sort(visited);
    REQUIRE(std::ranges::find(visited, "/root/a") != visited.end());
    REQUIRE(std::ranges::find(visited, "/root/a/file1.txt") != visited.end());
    REQUIRE(std::ranges::find(visited, "/root/b/file3.txt") != visited.end());
}

TEST_CASE("walkDirectoryRecursive stops when the consumer breaks out", "[FileSystem]")
{
    auto const fs = makeTree();

    // Breaking out after the first entry is exactly how `find` aborts on Ctrl+C:
    // the lazy walk must not keep producing entries once the consumer stops.
    // A conditional break rather than an unconditional one, which leaves the loop's own advance
    // unreachable (MSVC C4702).
    constexpr auto StopAfter = 1;
    auto visitCount = 0;
    for ([[maybe_unused]] auto const& entry: fs.walkDirectoryRecursive("/root"))
    {
        if (++visitCount == StopAfter)
            break;
    }

    REQUIRE(visitCount == StopAfter);
}

TEST_CASE("walkDirectoryRecursive yields each directory before its own contents", "[FileSystem]")
{
    auto const fs = makeTree();

    // Parents must precede their children (pre-order), the contract cp/rm depend on. Record the
    // order each path is first seen and assert every entry comes after its parent directory.
    auto order = std::vector<std::string> {};
    for (auto const& entry: fs.walkDirectoryRecursive("/root"))
        order.push_back(entry.path.generic_string());

    auto const indexOf = [&](std::string const& p) {
        return std::distance(order.begin(), std::ranges::find(order, p));
    };
    REQUIRE(indexOf("/root/a") < indexOf("/root/a/file1.txt"));
    REQUIRE(indexOf("/root/a") < indexOf("/root/a/file2.txt"));
    REQUIRE(indexOf("/root/b") < indexOf("/root/b/file3.txt"));
}

TEST_CASE("walkDirectoryRecursive yields nothing for a non-directory and reports the error", "[FileSystem]")
{
    auto const fs = makeTree();

    auto visitCount = 0;
    auto ec = std::error_code {};
    for ([[maybe_unused]] auto const& entry: fs.walkDirectoryRecursive("/does/not/exist", &ec))
        ++visitCount;

    REQUIRE(visitCount == 0);
    // The error channel lets destructive callers (cp/mv/rm) tell "empty directory" apart from
    // "could not enumerate", so they don't act on a partial/empty walk.
    REQUIRE(ec);
}

TEST_CASE("isExecutableFile accepts a file carrying an execute bit", "[FileSystem]")
{
    auto fs = InMemoryFileSystem {};
    fs.addExecutable("/usr/bin/tool");

    CHECK(fs.isExecutableFile("/usr/bin/tool"));
}

TEST_CASE("isExecutableFile rejects a regular file without an execute bit", "[FileSystem]")
{
    // Mirrors the POSIX rule every PATH lookup relies on, execvp(3)'s as much as a command
    // interpreter's: a readable/writable but non-executable file on PATH is not a runnable
    // command.
    auto fs = InMemoryFileSystem {};
    fs.addFile("/usr/bin/data", "contents");

    CHECK_FALSE(fs.isExecutableFile("/usr/bin/data"));
}

#ifndef _WIN32
TEST_CASE("isExecutableFile agrees with the real filesystem on the execute bit", "[FileSystem]")
{
    // The tests above pin InMemoryFileSystem's behaviour, which on its own only proves the
    // model is self-consistent. This one anchors it to the real thing: code that resolves
    // commands on PATH through a FileSystem is typically tested entirely against the model, so
    // if the two ever disagree about the execute bit, every one of those tests would agree with
    // each other and be wrong together.
    auto const& fs = core::platform::NativeFileSystem::instance();

    auto const path = fs.createTempFile("core_execbit_test");
    REQUIRE(path.has_value());

    // A failing REQUIRE below throws out of the test case, so removal cannot be a trailing
    // statement -- it would leave the file behind in the system temp directory.
    struct Remover
    {
        FileSystem const& fs;
        std::filesystem::path path;

        ~Remover() { [[maybe_unused]] auto const removed = fs.remove(path); }
    } const remover { .fs = fs, .path = *path };

    REQUIRE(fs.setPermissions(*path, std::filesystem::perms::owner_read).has_value());
    CHECK_FALSE(fs.isExecutableFile(*path));

    REQUIRE(fs.setPermissions(*path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec)
                .has_value());
    CHECK(fs.isExecutableFile(*path));

    // No removal here: that is the guard's job, exactly as its comment says.
}
#endif

TEST_CASE("isExecutableFile rejects directories and missing paths", "[FileSystem]")
{
    auto fs = InMemoryFileSystem {};
    fs.addDirectory("/usr/bin");

    CHECK_FALSE(fs.isExecutableFile("/usr/bin"));
    CHECK_FALSE(fs.isExecutableFile("/usr/bin/nope"));
}

TEST_CASE("isExecutableFile follows the execute bit of a symlink entry", "[FileSystem]")
{
    auto fs = InMemoryFileSystem {};
    fs.addSymlink("/usr/bin/link", "/opt/real");
    REQUIRE(fs.setPermissions("/usr/bin/link",
                              std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec)
                .has_value());

    CHECK(fs.isExecutableFile("/usr/bin/link"));
}

// ============================================================================
// openRead error reporting
// ============================================================================

TEST_CASE("openRead names the reason it failed", "[FileSystem]")
{
    // Every backend distinguishes the same three cases, so a caller can report what the
    // filesystem said instead of inferring it from a follow-up probe.
    auto fs = InMemoryFileSystem {};
    fs.addDirectory("/root/dir");
    fs.addFile("/root/readable.txt", "content");
    fs.addFile("/root/locked.txt", "secret");
    fs.denyAccess("/root/locked.txt");

    CHECK(fs.openRead("/root/readable.txt").has_value());

    auto const missing = fs.openRead("/root/absent.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == "No such file or directory");

    auto const directory = fs.openRead("/root/dir");
    REQUIRE_FALSE(directory.has_value());
    CHECK(directory.error() == "Is a directory");

    auto const denied = fs.openRead("/root/locked.txt");
    REQUIRE_FALSE(denied.has_value());
    CHECK(denied.error() == "Permission denied");
}

TEST_CASE("openRead reports the operating system's own reason", "[FileSystem]")
{
    auto const& fs = core::platform::NativeFileSystem::instance();
    auto const dir = core::testing::ScopedTempDir { "core_openread" };

    // A directory opens successfully on POSIX and fails only on the first read, so it has to
    // be refused explicitly rather than surfacing as an empty file.
    auto const directory = fs.openRead(dir.path());
    REQUIRE_FALSE(directory.has_value());
    CHECK(directory.error() == "Is a directory");

    // errno's text, not a guess: distinct from the directory case above and from success.
    auto const missing = fs.openRead(dir / "absent.txt");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == std::error_code(ENOENT, std::generic_category()).message());
}

TEST_CASE("the in-memory model classifies a symlink by its target", "[FileSystem]")
{
    // std::filesystem follows symlinks for is_regular_file()/is_directory() and only reports
    // the link itself through is_symlink(); the model has to agree or a consumer written
    // against the real filesystem behaves differently under injection.
    auto fs = InMemoryFileSystem {};
    fs.addFile("/root/target.txt", "content");
    fs.addDirectory("/root/dir");
    fs.addSymlink("/root/to-file", "/root/target.txt");
    fs.addSymlink("/root/to-dir", "/root/dir");
    fs.addSymlink("/root/dangling", "/root/absent");

    CHECK(fs.isRegularFile("/root/to-file"));
    CHECK(fs.isDirectory("/root/to-dir"));
    CHECK_FALSE(fs.isRegularFile("/root/dangling"));

    // is_symlink() does not follow: the link is still a link.
    CHECK(fs.isSymlink("/root/to-file"));
    CHECK_FALSE(fs.isSymlink("/root/target.txt"));

    auto const opened = fs.openRead("/root/to-file");
    REQUIRE(opened.has_value());
    // Read through a string stream rather than std::istreambuf_iterator, which GCC 14's -O2
    // -Wnull-dereference trips over inside libstdc++'s streambuf.
    auto contents = std::ostringstream {};
    contents << (*opened)->rdbuf();
    CHECK(contents.str() == "content");

    // A cycle terminates rather than hanging.
    fs.addSymlink("/root/loop-a", "/root/loop-b");
    fs.addSymlink("/root/loop-b", "/root/loop-a");
    CHECK_FALSE(fs.isRegularFile("/root/loop-a"));
}

// ============================================================================
// Write streams
// ============================================================================

TEST_CASE("the write streams answer alike in the model and on the real filesystem", "[FileSystem]")
{
    // openWrite() and openReadWrite() had no test at all, which is how the model came to append
    // on every write regardless of where the stream stood. A consumer is written against one
    // backend and tested against the other, so the two have to agree.
    auto const model = InMemoryFileSystem {};
    auto const dir = core::testing::ScopedTempDir { "core_streams" };

    auto const fromModel = runStreamScript(model, "/root/file.txt");
    auto const fromNative = runStreamScript(core::platform::NativeFileSystem::instance(), dir / "file.txt");

    CHECK(fromNative.afterTruncatingWrite == "0123456789");
    CHECK(fromNative.afterAppend == "0123456789ABC");
    CHECK(fromNative.afterOverwriteInPlace == "xy23456789ABC");
    CHECK(fromNative.readAfterOverwrite == "4567");
    CHECK(fromModel == fromNative);
}

TEST_CASE("the model's read-write stream survives a write that reallocates the file", "[FileSystem]")
{
    // The stream is handed get-area pointers into the std::string that holds the file. A write
    // that grows the string past its capacity reallocates it, and every one of those pointers
    // then names freed memory: a heap-use-after-free on the next read under AddressSanitizer,
    // and whatever the allocator left behind without it.
    // Long enough that the seed itself lives on the heap: a short string sits inside the
    // std::string object, which the growing write moves off but never frees.
    auto const fs = InMemoryFileSystem {};
    REQUIRE(fs.writeFile("/root/data.bin", std::string(64, 's')).has_value());

    auto stream = fs.openReadWrite("/root/data.bin");
    REQUIRE(stream.has_value());

    auto const payload = std::string(4096, 'x');
    (*stream)->write(payload.data(), static_cast<std::streamsize>(payload.size()));

    // The write started at the beginning and ran past the end, so there is nothing left to read.
    // Reading anyway is what dereferences the stale get area.
    auto byte = char {};
    (*stream)->read(&byte, 1);
    CHECK((*stream)->gcount() == 0);

    (*stream)->clear();
    (*stream)->seekg(0);
    (*stream)->read(&byte, 1);
    CHECK(byte == 'x');

    stream->reset();
    CHECK(fs.readFile("/root/data.bin") == payload);
}

#ifndef _WIN32
TEST_CASE("isExecutableFile classifies a symlink by what it points at", "[FileSystem]")
{
    // "Directories always return false", the declaration says, and a plain directory is rejected
    // -- but a symlink to one slipped through: the guard accepted any symlink and then read the
    // permissions of the followed target, which for a directory carry the execute bit that makes
    // it searchable. A PATH lookup that trusts that runs the directory, fails with EACCES and
    // never tries the next entry.
    //
    // Windows classifies through GetFileAttributesW, which does not follow reparse points and
    // already answers false for a directory symlink.
    namespace fs = std::filesystem;
    auto const& backend = core::platform::NativeFileSystem::instance();
    auto const dir = core::testing::ScopedTempDir { "core_execsymlink" };

    auto const subdirectory = dir / "subdir";
    REQUIRE(backend.createDirectory(subdirectory).has_value());

    auto ec = std::error_code {};
    fs::create_directory_symlink(subdirectory, dir / "to-dir", ec);
    if (ec)
        SKIP("this filesystem refuses symlinks: " + ec.message());

    CHECK_FALSE(backend.isExecutableFile(subdirectory));
    CHECK_FALSE(backend.isExecutableFile(dir / "to-dir"));

    // A symlink to an executable file stays executable: that is the case PATH lookups need.
    auto const tool = dir / "tool";
    REQUIRE(backend.writeFile(tool, "#!/bin/sh\n").has_value());
    REQUIRE(backend.setPermissions(tool, fs::perms::owner_read | fs::perms::owner_exec).has_value());
    fs::create_symlink(tool, dir / "to-tool", ec);
    REQUIRE_FALSE(ec);
    CHECK(backend.isExecutableFile(dir / "to-tool"));

    // A dangling symlink names nothing a process can execute.
    fs::create_symlink(dir / "absent", dir / "dangling", ec);
    REQUIRE_FALSE(ec);
    CHECK_FALSE(backend.isExecutableFile(dir / "dangling"));
}
#endif

TEST_CASE("the model tells apart two paths the native narrow encoding cannot", "[FileSystem]")
{
    // Every key of this filesystem is a path run through stripTrailingSeparator(). While that
    // went via generic_string(), two paths the platform's narrow encoding cannot spell -- which
    // on Windows is anything outside the ANSI code page -- collapsed onto the same replacement
    // spelling, and one file answered for the other.
    auto fs = InMemoryFileSystem {};
    auto const first = std::filesystem::path { std::u8string { u8"/tmp/\u65e5/data" } };
    auto const second = std::filesystem::path { std::u8string { u8"/tmp/\u672c/data" } };
    fs.addFile(first, "one");
    fs.addFile(second, "two");

    CHECK(fs.readFile(first) == "one");
    CHECK(fs.readFile(second) == "two");
}

TEST_CASE("createDirectory says which of the two reasons it failed for", "[FileSystem]")
{
    // create_directory() answering false with no error means the directory is already there.
    // The one path in this backend where a hard-coded string, not the operating system, picks
    // the message read that as "No such file or directory" -- which is the diagnosis for the
    // other way it fails, a missing parent, and sends a caller looking in the wrong place.
    auto const& backend = core::platform::NativeFileSystem::instance();
    auto const dir = core::testing::ScopedTempDir { "core_mkdir" };

    REQUIRE(backend.createDirectory(dir / "sub").has_value());

    auto const again = backend.createDirectory(dir / "sub");
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().contains(std::make_error_code(std::errc::file_exists).message()));

    auto const missingParent = backend.createDirectory(dir / "absent" / "sub");
    REQUIRE_FALSE(missingParent.has_value());
    CHECK(again.error() != missingParent.error());
}

#ifndef _WIN32
TEST_CASE("rename reports why the recase failed, not why the first attempt did", "[FileSystem]")
{
    // A rename that changes only lettercase is retried in two hops through a temporary name.
    // renameViaTemporary()'s comment promises the second attempt's reason, but the error it set
    // was never read and the caller got the first attempt's instead.
    //
    // Here the direct rename fails because the destination is a non-empty directory, and the
    // retry fails for a different reason: the temporary name is nine characters longer than the
    // destination's, which already sits at NAME_MAX.
    auto const& backend = core::platform::NativeFileSystem::instance();
    auto const dir = core::testing::ScopedTempDir { "core_recase" };

    constexpr auto NameMax = 255;
    auto const lower = std::string(NameMax, 'a');
    auto upper = lower;
    upper[0] = 'A';

    // Both spellings have to name two entries for the direct rename to fail over the destination
    // rather than resolve to the source, which a case-insensitive filesystem -- macOS's default
    // volume format -- cannot arrange. The logic under test is the same everywhere, and the
    // case-sensitive hosts cover it.
    REQUIRE(backend.createDirectory(dir / lower).has_value());
    if (backend.exists(dir / upper))
        SKIP("this filesystem is case-insensitive, so the two directories cannot both exist");

    REQUIRE(backend.createDirectory(dir / upper).has_value());
    for (auto const& name: { lower, upper })
        REQUIRE(backend.writeFile(dir / name / "occupant", "x").has_value());

    if (backend.createDirectory(dir / (upper + ".recase-0")).has_value())
        SKIP("this filesystem accepts names past NAME_MAX, so the retry would not fail here");

    auto const renamed = backend.rename(dir / lower, dir / upper);
    REQUIRE_FALSE(renamed.has_value());
    CHECK(renamed.error().contains(std::make_error_code(std::errc::filename_too_long).message()));
    CHECK_FALSE(renamed.error().contains(std::make_error_code(std::errc::directory_not_empty).message()));

    // The rollback put it back under its original name, so nothing is stranded.
    CHECK(backend.isDirectory(dir / lower));
}
#endif
