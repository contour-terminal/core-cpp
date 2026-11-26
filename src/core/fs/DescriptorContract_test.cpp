// SPDX-License-Identifier: Apache-2.0
//
// What every IDescriptor owes its caller, whichever file system is under it. Each case GENERATEs
// the fixture kinds it holds for: the in-memory model and the native descriptors.
#include <core/fs/Descriptor.hpp>
#include <core/fs/DescriptorTestSupport.hpp>
#include <core/fs/FsError.hpp>
#include <core/fs/PathPolicy.hpp>
#include <core/fs/Rights.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using core::fs::Advice;
using core::fs::DescriptorFlags;
using core::fs::DescriptorStat;
using core::fs::DescriptorType;
using core::fs::FileTime;
using core::fs::FsErrorCode;
using core::fs::FsResult;
using core::fs::IDescriptor;
using core::fs::MetadataHash;
using core::fs::NoChange;
using core::fs::OpenFlags;
using core::fs::Operation;
using core::fs::SetToNow;
using core::fs::StreamEnd;
using core::fs::SymlinkPolicy;
using core::fs::test::bytesOf;
using core::fs::test::DescriptorFixture;
using core::fs::test::FixtureKind;
using core::fs::test::NowSource;
using core::fs::test::Outcome;
using core::fs::test::outcomeOf;
using core::fs::test::SymlinkSupport;
using core::fs::test::textOf;

TEST_CASE("contract: written bytes read back with an end marker", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    using enum core::fs::OpenFlags;
    auto file = fixture.run(
        fixture.root().openAt(core::fs::SymlinkPolicy::NoFollow,
                              "f.txt",
                              Create | Exclusive,
                              core::fs::DescriptorFlags::Read | core::fs::DescriptorFlags::Write));
    REQUIRE(file.has_value());
    CHECK(fixture.run((*file)->write(bytesOf("hello"), 0)).value() == 5);
    CHECK(textOf(fixture.run((*file)->read(64, 0)).value().bytes) == "hello");
    CHECK(fixture.run((*file)->read(64, 5)).value().end == core::fs::StreamEnd::Reached);
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(core::fs::SymlinkPolicy::NoFollow,
                                                      "f.txt",
                                                      Create | Exclusive,
                                                      core::fs::DescriptorFlags::Write)))
          == core::fs::FsErrorCode::Exist);
}

TEST_CASE("contract: a path that climbs above its descriptor is NotPermitted", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("a")).has_value());
    fixture.createFile("x", "inside");

    auto const escaped = fixture.run(
        fixture.root().openAt(SymlinkPolicy::NoFollow, "a/../../x", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE_FALSE(escaped.has_value());
    CHECK(escaped.error().code == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "..")))
          == FsErrorCode::NotPermitted);
    CHECK(fixture
              .run(fixture.root().openAt(
                  SymlinkPolicy::NoFollow, "a/../x", OpenFlags::None, DescriptorFlags::Read))
              .has_value());
}

TEST_CASE("contract: a read-only preopen refuses every mutation and changes nothing", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    fixture.createFile("f", "content");
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("d")).has_value());
    auto readOnly = fixture.openRoot(DescriptorFlags::Read);
    REQUIRE(readOnly.has_value());
    auto& dir = **readOnly;
    auto const before = fixture.run(dir.statAt(SymlinkPolicy::NoFollow, "f"));
    REQUIRE(before.has_value());
    fixture.clock().advance(std::chrono::hours { 1 });

    auto const refusal = [&fixture](auto task) {
        auto const result = fixture.run(std::move(task));
        REQUIRE_FALSE(result.has_value());
        return result.error().code;
    };
    CHECK(refusal(dir.openAt(SymlinkPolicy::NoFollow, "f", OpenFlags::None, DescriptorFlags::Write))
          == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.openAt(SymlinkPolicy::NoFollow, "g", OpenFlags::Create, DescriptorFlags::Read))
          == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.openAt(SymlinkPolicy::NoFollow, "f", OpenFlags::Truncate, DescriptorFlags::Read))
          == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.createDirectoryAt("e")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.unlinkFileAt("f")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.removeDirectoryAt("d")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.renameAt("f", &dir, "g")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.symlinkAt("f", "l")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.linkAt(SymlinkPolicy::NoFollow, "f", &dir, "h")) == FsErrorCode::NotPermitted);
    CHECK(refusal(dir.setTimesAt(SymlinkPolicy::NoFollow, "f", SetToNow {}, SetToNow {}))
          == FsErrorCode::NotPermitted);

    for (auto const* name: { "e", "g", "h", "l" })
    {
        CAPTURE(name);
        CHECK(outcomeOf(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, name)))
              == FsErrorCode::NoEntry);
    }
    auto const after = fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "f"));
    REQUIRE(after.has_value());
    CHECK(after->modified == before->modified);
    CHECK(after->accessed == before->accessed);
    CHECK(after->size == 7);
    CHECK(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "d"))->type
          == DescriptorType::Directory);
}

TEST_CASE("contract: a child opened with more flags than its parent is NotPermitted", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("d")).has_value());
    auto readOnly = fixture.openRoot(DescriptorFlags::Read);
    REQUIRE(readOnly.has_value());

    auto const wider =
        fixture.run((*readOnly)->openAt(SymlinkPolicy::NoFollow,
                                        "d",
                                        OpenFlags::Directory,
                                        DescriptorFlags::Read | DescriptorFlags::MutateDirectory));
    REQUIRE_FALSE(wider.has_value());
    CHECK(wider.error().code == FsErrorCode::NotPermitted);

    auto const narrower = fixture.run(
        (*readOnly)->openAt(SymlinkPolicy::NoFollow, "d", OpenFlags::Directory, DescriptorFlags::Read));
    REQUIRE(narrower.has_value());
    CHECK((*narrower)->flags() == DescriptorFlags::Read);
}

TEST_CASE("contract: an open the parent may not make is NotPermitted before its flags are judged",
          "[fs][contract][rights]")
{
    // wasmtime's order: the parent's rights first, then Directory with Create is Invalid.
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("d")).has_value());
    auto readOnly = fixture.openRoot(DescriptorFlags::Read);
    REQUIRE(readOnly.has_value());

    CHECK(outcomeOf(fixture.run((*readOnly)->openAt(
              SymlinkPolicy::NoFollow, "d", OpenFlags::Directory | OpenFlags::Create, DescriptorFlags::Read)))
          == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run((*readOnly)->openAt(SymlinkPolicy::NoFollow,
                                                    "d",
                                                    OpenFlags::Directory,
                                                    DescriptorFlags::Read | DescriptorFlags::Write)))
          == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::NoFollow, "d", OpenFlags::Directory | OpenFlags::Create, DescriptorFlags::Read)))
          == FsErrorCode::Invalid);
}

TEST_CASE("contract: a read returns at most maxReadChunk bytes", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    fixture.createFile("big", std::string(DescriptorFixture::MaxReadChunk * 3, 'x'));
    auto file = fixture.run(
        fixture.root().openAt(SymlinkPolicy::NoFollow, "big", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE(file.has_value());

    auto const chunk = fixture.run((*file)->read(std::size_t { 1 } << 30U, 0));
    REQUIRE(chunk.has_value());
    CHECK(chunk->bytes.size() == DescriptorFixture::MaxReadChunk);
    CHECK(chunk->end == StreamEnd::More);
}

TEST_CASE("contract: a foreign descriptor passed to renameAt is CrossDevice", "[fs][contract]")
{
    auto const kind = GENERATE(FixtureKind::InMemory, FixtureKind::Native);
    auto fixture = DescriptorFixture::create(kind);
    auto other = DescriptorFixture::create(kind);
    fixture.createFile("f", "content");

    auto const moved = fixture.run(fixture.root().renameAt("f", &other.root(), "g"));
    REQUIRE_FALSE(moved.has_value());
    CHECK(moved.error().code == FsErrorCode::CrossDevice);
    CHECK(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "f")).has_value());
    CHECK(outcomeOf(other.run(other.root().statAt(SymlinkPolicy::NoFollow, "g"))) == FsErrorCode::NoEntry);
}

TEST_CASE("contract: names compare under the policy's case rule", "[fs][contract]")
{
    // In-memory only, for good: the native case rule is the volume's, not the policy's.
    SECTION("posix: names differing in case are two names")
    {
        auto fixture = DescriptorFixture::create(FixtureKind::InMemory, core::fs::posixPathPolicy());
        fixture.createFile("Name.txt", "upper");
        CHECK(fixture
                  .run(fixture.root().openAt(SymlinkPolicy::NoFollow,
                                             "name.txt",
                                             OpenFlags::Create | OpenFlags::Exclusive,
                                             DescriptorFlags::Write))
                  .has_value());
    }
    SECTION("windows: names differing in case are one name")
    {
        auto fixture = DescriptorFixture::create(FixtureKind::InMemory, core::fs::windowsPathPolicy());
        fixture.createFile("Name.txt", "upper");
        CHECK(outcomeOf(fixture.run(fixture.root().openAt(SymlinkPolicy::NoFollow,
                                                          "name.txt",
                                                          OpenFlags::Create | OpenFlags::Exclusive,
                                                          DescriptorFlags::Write)))
              == FsErrorCode::Exist);
        auto file = fixture.run(fixture.root().openAt(
            SymlinkPolicy::NoFollow, "NAME.TXT", OpenFlags::None, DescriptorFlags::Read));
        REQUIRE(file.has_value());
        CHECK(textOf(fixture.run((*file)->read(64, 0)).value().bytes) == "upper");
    }
}

TEST_CASE("contract: a symbolic link is followed only beneath its descriptor", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
        SKIP("this file system cannot create symbolic links");
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("a")).has_value());
    fixture.createFile("target", "through the link");

    CHECK(outcomeOf(fixture.run(fixture.root().symlinkAt("/etc/passwd", "absolute")))
          == FsErrorCode::NotPermitted);
    REQUIRE(fixture.run(fixture.root().symlinkAt("../target", "a/inside")).has_value());
    REQUIRE(fixture.run(fixture.root().symlinkAt("../../outside", "a/escape")).has_value());
    REQUIRE(fixture.run(fixture.root().symlinkAt("loop", "loop")).has_value());

    auto inside = fixture.run(
        fixture.root().openAt(SymlinkPolicy::Follow, "a/inside", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE(inside.has_value());
    CHECK(textOf(fixture.run((*inside)->read(64, 0)).value().bytes) == "through the link");
    CHECK(fixture.run(fixture.root().readlinkAt("a/inside")).value() == "../target");
    CHECK(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "a/inside"))->type
          == DescriptorType::SymbolicLink);
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::NoFollow, "a/inside", OpenFlags::None, DescriptorFlags::Read)))
          == FsErrorCode::Loop);

    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::Follow, "a/escape", OpenFlags::None, DescriptorFlags::Read)))
          == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run(fixture.root().statAt(SymlinkPolicy::Follow, "loop"))) == FsErrorCode::Loop);
    CHECK(outcomeOf(
              fixture.run(fixture.root().linkAt(SymlinkPolicy::Follow, "target", &fixture.root(), "hard")))
          == FsErrorCode::Invalid);
}

TEST_CASE("contract: a directory descriptor never carries Write", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("d")).has_value());
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::NoFollow, "d", OpenFlags::None, DescriptorFlags::Read | DescriptorFlags::Write)))
          == FsErrorCode::IsDirectory);
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::NoFollow, "d", OpenFlags::Directory | OpenFlags::Create, DescriptorFlags::Read)))
          == FsErrorCode::Invalid);
}

namespace
{

/// What a rights case calls its operation on.
enum class Holder : std::uint8_t
{
    File,      ///< `f`, a regular file; it can carry Read and Write.
    Directory, ///< The root; it can carry Read and MutateDirectory.
};

/// One operation of the rights table, and how to call it on a holder in a fixture that holds `f`
/// (a file with content), `sub` (an empty directory) and, where the file system has them, `link` (a
/// symbolic link to `f`).
struct RightsCase
{
    Operation operation;
    DescriptorFlags required; ///< What its row must say, pinned here rather than read from the table.
    Holder holder;
    std::function<Outcome(DescriptorFixture&, IDescriptor&)> call;
};

std::vector<RightsCase> rightsCases()
{
    using enum Operation;
    using enum Holder;
    auto const nf = SymlinkPolicy::NoFollow;
    return {
        { Read,
          DescriptorFlags::Read,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.read(1, 0))); } },
        { Write,
          DescriptorFlags::Write,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.write(bytesOf("x"), 0))); } },
        { Append,
          DescriptorFlags::Write,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.append(bytesOf("x")))); } },
        { Advise,
          DescriptorFlags::None,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.advise(0, 0, Advice::Normal))); } },
        { Sync, DescriptorFlags::None, File, [](auto& fx, auto& d) { return outcomeOf(fx.run(d.sync())); } },
        { SetSize,
          DescriptorFlags::Write,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.setSize(1))); } },
        { SetTimes,
          DescriptorFlags::Write | DescriptorFlags::MutateDirectory,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.setTimes(SetToNow {}, SetToNow {}))); } },
        { Stat, DescriptorFlags::None, File, [](auto& fx, auto& d) { return outcomeOf(fx.run(d.stat())); } },
        { MetadataHash,
          DescriptorFlags::None,
          File,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.metadataHash())); } },
        { ReadDirectory,
          DescriptorFlags::Read,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.readDirectory())); } },
        { OpenAt,
          DescriptorFlags::Read,
          Directory,
          [nf](auto& fx, auto& d) {
              return outcomeOf(fx.run(d.openAt(nf, "f", OpenFlags::None, DescriptorFlags::None)));
          } },
        { CreateDirectoryAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.createDirectoryAt("new"))); } },
        { StatAt,
          DescriptorFlags::Read,
          Directory,
          [nf](auto& fx, auto& d) { return outcomeOf(fx.run(d.statAt(nf, "f"))); } },
        { SetTimesAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [nf](auto& fx, auto& d) {
              return outcomeOf(fx.run(d.setTimesAt(nf, "f", SetToNow {}, SetToNow {})));
          } },
        { LinkAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [nf](auto& fx, auto& d) { return outcomeOf(fx.run(d.linkAt(nf, "f", &d, "hard"))); } },
        { ReadlinkAt,
          DescriptorFlags::Read,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.readlinkAt("link"))); } },
        { RemoveDirectoryAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.removeDirectoryAt("sub"))); } },
        { RenameAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.renameAt("f", &d, "renamed"))); } },
        { SymlinkAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.symlinkAt("f", "newlink"))); } },
        { UnlinkFileAt,
          DescriptorFlags::MutateDirectory,
          Directory,
          [](auto& fx, auto& d) { return outcomeOf(fx.run(d.unlinkFileAt("f"))); } },
        { MetadataHashAt,
          DescriptorFlags::Read,
          Directory,
          [nf](auto& fx, auto& d) { return outcomeOf(fx.run(d.metadataHashAt(nf, "f"))); } },
    };
}

/// Calls @p rightsCase on a holder carrying @p flags, in a fixture of its own.
Outcome callWith(FixtureKind kind, RightsCase const& rightsCase, DescriptorFlags flags)
{
    auto fixture = DescriptorFixture::create(kind);
    fixture.createFile("f", "content");
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("sub")).has_value());
    if (fixture.requireSymlinks() == SymlinkSupport::Available)
        REQUIRE(fixture.run(fixture.root().symlinkAt("f", "link")).has_value());
    auto holder =
        rightsCase.holder == Holder::File
            ? fixture.run(fixture.root().openAt(SymlinkPolicy::NoFollow, "f", OpenFlags::None, flags))
            : fixture.openRoot(flags);
    REQUIRE(holder.has_value());
    return rightsCase.call(fixture, **holder);
}

DescriptorFlags without(DescriptorFlags set, DescriptorFlags removed)
{
    return static_cast<DescriptorFlags>(static_cast<std::uint8_t>(set) & ~static_cast<std::uint8_t>(removed));
}

} // namespace

TEST_CASE("contract: every operation needs the rights its row names", "[fs][contract][rights]")
{
    auto const kind = GENERATE(FixtureKind::InMemory, FixtureKind::Native);
    auto const cases = rightsCases();
    REQUIRE(cases.size() == static_cast<std::size_t>(Operation::Last));
    for (auto const& row: core::fs::rightsTable())
    {
        auto const rightsCase = std::ranges::find(cases, row.operation, &RightsCase::operation);
        REQUIRE(rightsCase != cases.end());
        CAPTURE(static_cast<int>(row.operation), static_cast<int>(row.required));
        CHECK(row.required == rightsCase->required);
        if (rightsCase->required == DescriptorFlags::None)
        {
            CHECK(callWith(kind, *rightsCase, DescriptorFlags::None) != FsErrorCode::NotPermitted);
            continue;
        }
        auto const all = rightsCase->holder == Holder::File
                             ? DescriptorFlags::Read | DescriptorFlags::Write
                             : DescriptorFlags::Read | DescriptorFlags::MutateDirectory;
        CHECK(callWith(kind, *rightsCase, without(all, rightsCase->required)) == FsErrorCode::NotPermitted);
        CHECK(callWith(kind, *rightsCase, all) != FsErrorCode::NotPermitted);
    }

    // SetTimes's row offers a choice, and the loop above takes the file's side of it (Write); the
    // directory's side is MutateDirectory.
    auto fixture = DescriptorFixture::create(kind);
    auto readOnly = fixture.openRoot(DescriptorFlags::Read);
    auto mutating = fixture.openRoot(DescriptorFlags::Read | DescriptorFlags::MutateDirectory);
    REQUIRE(readOnly.has_value());
    REQUIRE(mutating.has_value());
    CHECK(outcomeOf(fixture.run((*readOnly)->setTimes(SetToNow {}, SetToNow {})))
          == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run((*mutating)->setTimes(SetToNow {}, SetToNow {}))) == std::nullopt);
}

TEST_CASE("contract: a file descriptor resolves no path and carries no MutateDirectory", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    fixture.createFile("f", "content");
    fixture.createFile("g", "other");

    auto file = fixture.run(fixture.root().openAt(SymlinkPolicy::NoFollow,
                                                  "f",
                                                  OpenFlags::None,
                                                  DescriptorFlags::Read | DescriptorFlags::MutateDirectory));
    REQUIRE(file.has_value());
    CHECK((*file)->flags() == DescriptorFlags::Read);
    CHECK(outcomeOf(fixture.run((*file)->statAt(SymlinkPolicy::NoFollow, "x"))) == FsErrorCode::NotDirectory);
    CHECK(outcomeOf(fixture.run(
              (*file)->openAt(SymlinkPolicy::NoFollow, "x", OpenFlags::None, DescriptorFlags::Read)))
          == FsErrorCode::NotDirectory);
    CHECK(outcomeOf(fixture.run((*file)->createDirectoryAt("x"))) == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run(fixture.root().renameAt("g", file->get(), "x")))
          == FsErrorCode::NotPermitted);
    CHECK(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "g")).has_value());
    CHECK(outcomeOf(fixture.run((*file)->setTimes(SetToNow {}, SetToNow {}))) == FsErrorCode::NotPermitted);

    auto writable =
        fixture.run(fixture.root().openAt(SymlinkPolicy::NoFollow,
                                          "f",
                                          OpenFlags::None,
                                          DescriptorFlags::Write | DescriptorFlags::MutateDirectory));
    REQUIRE(writable.has_value());
    CHECK((*writable)->flags() == DescriptorFlags::Write);
    CHECK(fixture.run((*writable)->setTimes(SetToNow {}, SetToNow {})).has_value());
}

TEST_CASE("contract: a trailing slash requires a directory", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto& root = fixture.root();
    fixture.createFile("file", "content");
    REQUIRE(fixture.run(root.createDirectoryAt("dir")).has_value());
    auto const nf = SymlinkPolicy::NoFollow;
    auto const notDirectory = Outcome { FsErrorCode::NotDirectory };

    SECTION("a file named with a trailing slash")
    {
        CHECK(outcomeOf(fixture.run(root.openAt(nf, "file/", OpenFlags::None, DescriptorFlags::Read)))
              == notDirectory);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "file/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "file/."))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.unlinkFileAt("file/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.removeDirectoryAt("file/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("file/", &root, "moved"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("file", &root, "moved/"))) == notDirectory);
        CHECK(fixture.run(root.statAt(nf, "file"))->type == DescriptorType::RegularFile);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "moved"))) == FsErrorCode::NoEntry);
    }
    SECTION("nothing but a directory is created with a trailing slash")
    {
        auto const created =
            outcomeOf(fixture.run(root.openAt(nf, "new.txt/", OpenFlags::Create, DescriptorFlags::Write)));
        CHECK((created == notDirectory || created == FsErrorCode::IsDirectory));
        CHECK(outcomeOf(fixture.run(root.symlinkAt("file", "newlink/"))).has_value());
        CHECK(outcomeOf(fixture.run(root.linkAt(nf, "file", &root, "hard/"))).has_value());
        for (auto const* name: { "new.txt", "newlink", "hard" })
        {
            CAPTURE(name);
            CHECK(outcomeOf(fixture.run(root.statAt(nf, name))) == FsErrorCode::NoEntry);
        }
        REQUIRE(fixture.run(root.createDirectoryAt("made/")).has_value());
        CHECK(fixture.run(root.statAt(nf, "made"))->type == DescriptorType::Directory);
    }
    SECTION("a directory named with a trailing slash")
    {
        CHECK(fixture.run(root.statAt(nf, "dir/"))->type == DescriptorType::Directory);
        CHECK(outcomeOf(fixture.run(root.unlinkFileAt("dir/"))) == FsErrorCode::IsDirectory);
        REQUIRE(fixture.run(root.renameAt("dir/", &root, "moved/")).has_value());
        REQUIRE(fixture.run(root.removeDirectoryAt("moved/")).has_value());
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "moved"))) == FsErrorCode::NoEntry);
    }
    SECTION("a trailing slash follows a final symbolic link")
    {
        if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
            SKIP("this file system cannot create symbolic links");
        REQUIRE(fixture.run(root.symlinkAt("dir", "dirlink")).has_value());
        REQUIRE(fixture.run(root.symlinkAt("file", "filelink")).has_value());
        CHECK(fixture.run(root.statAt(nf, "dirlink"))->type == DescriptorType::SymbolicLink);
        CHECK(fixture.run(root.statAt(nf, "dirlink/"))->type == DescriptorType::Directory);
        CHECK(fixture.run(root.statAt(nf, "dirlink/."))->type == DescriptorType::Directory);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "filelink/"))) == notDirectory);

        // Removing or moving `dirlink/` acts on the link, which is no directory: Linux answers
        // ENOTDIR, and neither the link nor the directory it names may change.
        CHECK(outcomeOf(fixture.run(root.removeDirectoryAt("dirlink/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("dirlink/", &root, "moved"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("dir", &root, "dirlink/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.unlinkFileAt("dirlink/"))) == notDirectory);
        CHECK(fixture.run(root.statAt(nf, "dirlink"))->type == DescriptorType::SymbolicLink);
        CHECK(fixture.run(root.statAt(nf, "dir"))->type == DescriptorType::Directory);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "moved"))) == FsErrorCode::NoEntry);

        // The same on a link to a file: the trailing `/` makes neither the link nor the file the
        // object of the call.
        CHECK(outcomeOf(fixture.run(root.removeDirectoryAt("filelink/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("filelink/", &root, "moved"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.renameAt("file", &root, "filelink/"))) == notDirectory);
        CHECK(outcomeOf(fixture.run(root.unlinkFileAt("filelink/"))) == notDirectory);
        CHECK(fixture.run(root.statAt(nf, "filelink"))->type == DescriptorType::SymbolicLink);
        CHECK(fixture.run(root.statAt(nf, "file"))->type == DescriptorType::RegularFile);
        CHECK(outcomeOf(fixture.run(root.statAt(nf, "moved"))) == FsErrorCode::NoEntry);
    }
}

TEST_CASE("contract: a directory named through its own dot is not removed", "[fs][contract]")
{
    // `dir/.` names the directory itself, not an entry of its parent: Linux's rmdir answers EINVAL.
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto& root = fixture.root();
    REQUIRE(fixture.run(root.createDirectoryAt("dir")).has_value());

    CHECK(outcomeOf(fixture.run(root.removeDirectoryAt("dir/."))) == FsErrorCode::Invalid);
    CHECK(outcomeOf(fixture.run(root.removeDirectoryAt("dir/./"))) == FsErrorCode::Invalid);
    CHECK(fixture.run(root.statAt(SymlinkPolicy::NoFollow, "dir"))->type == DescriptorType::Directory);
}

TEST_CASE("contract: Create opens no directory and creates nothing through a link", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    REQUIRE(fixture.run(fixture.root().createDirectoryAt("dir")).has_value());
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(
              SymlinkPolicy::NoFollow, "dir", OpenFlags::Create, DescriptorFlags::Read)))
          == FsErrorCode::IsDirectory);

    if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
        SKIP("this file system cannot create symbolic links");
    REQUIRE(fixture.run(fixture.root().symlinkAt("missing", "dangling")).has_value());
    CHECK(outcomeOf(fixture.run(fixture.root().openAt(SymlinkPolicy::Follow,
                                                      "dangling",
                                                      OpenFlags::Create | OpenFlags::Exclusive,
                                                      DescriptorFlags::Write)))
          == FsErrorCode::Exist);
    CHECK(outcomeOf(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "missing")))
          == FsErrorCode::NoEntry);
}

TEST_CASE("contract: a link target is checked when the link is created", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
        SKIP("this file system cannot create symbolic links");
    CHECK(outcomeOf(fixture.run(fixture.root().symlinkAt(std::string { "a\0b", 3 }, "nul")))
          == FsErrorCode::Invalid);
    CHECK(outcomeOf(fixture.run(fixture.root().symlinkAt("", "empty"))) == FsErrorCode::NoEntry);
    CHECK(outcomeOf(fixture.run(fixture.root().symlinkAt("/abs", "absolute"))) == FsErrorCode::NotPermitted);
}

TEST_CASE("contract: a Windows drive in a link target is absolute", "[fs][contract]")
{
    // In-memory only, for good: it is the Windows policy's rule, run on every OS.
    auto fixture = DescriptorFixture::create(FixtureKind::InMemory, core::fs::windowsPathPolicy());
    for (auto const* target: { "C:\\x", "C:x", "c:/x" })
    {
        CAPTURE(target);
        CHECK(outcomeOf(fixture.run(fixture.root().symlinkAt(target, "drive"))) == FsErrorCode::NotPermitted);
    }
}

namespace
{

/// Opens `name` beneath @p fixture's root, `Read | Write`, creating it.
std::unique_ptr<IDescriptor> createReadWrite(DescriptorFixture const& fixture, std::string name)
{
    auto file = fixture.run(fixture.root().openAt(SymlinkPolicy::NoFollow,
                                                  std::move(name),
                                                  OpenFlags::Create | OpenFlags::Exclusive,
                                                  DescriptorFlags::Read | DescriptorFlags::Write));
    REQUIRE(file.has_value());
    return std::move(*file);
}

/// A native time comes from the kernel's clock, which may lag the C++ one by a tick and be stored
/// at a coarser grain: an instant counts as within a window this much wider on each side.
constexpr auto ClockSlack = std::chrono::seconds { 2 };

/// @return Whether @p time lies in [@p before, @p after], widened by @c ClockSlack.
bool within(FileTime time,
            std::chrono::system_clock::time_point before,
            std::chrono::system_clock::time_point after)
{
    return core::fs::test::fileTimeOf(before - ClockSlack) <= time
           && time <= core::fs::test::fileTimeOf(after + ClockSlack);
}

} // namespace

TEST_CASE("contract: stat reports type, size and a plausible modification time", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto const before = std::chrono::system_clock::now();
    fixture.createFile("f", "hello");
    auto const after = std::chrono::system_clock::now();

    auto file = fixture.run(
        fixture.root().openAt(SymlinkPolicy::NoFollow, "f", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE(file.has_value());
    auto const stat = fixture.run((*file)->stat());
    REQUIRE(stat.has_value());
    CHECK(stat->type == DescriptorType::RegularFile);
    CHECK(stat->size == 5);
    CHECK(stat->linkCount == 1);
    REQUIRE(stat->modified.has_value());
    // The model stamps the injected clock; a native file system, the kernel's.
    if (fixture.kind() == FixtureKind::InMemory)
        CHECK(*stat->modified == core::fs::test::fileTimeOf(fixture.clock().now()));
    else
        CHECK(within(*stat->modified, before, after));
    CHECK(fixture.run((*file)->type()).value() == DescriptorType::RegularFile);
    CHECK(fixture.run(fixture.root().stat())->type == DescriptorType::Directory);
    CHECK(fixture.run(fixture.root().statAt(SymlinkPolicy::NoFollow, "f"))->size == 5);
}

TEST_CASE("contract: setTimes sets an explicit instant, and SetToNow the file system's clock",
          "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto const file = createReadWrite(fixture, "f");
    // A multiple of 100 ns, the grain of a Windows FILETIME.
    auto const explicitTime = FileTime { .seconds = 1'600'000'000, .nanoseconds = 123'456'700 };

    REQUIRE(fixture.run(file->setTimes(explicitTime, explicitTime)).has_value());
    auto const set = fixture.run(file->stat());
    REQUIRE(set.has_value());
    CHECK(set->accessed == explicitTime);
    CHECK(set->modified == explicitTime);

    fixture.clock().advance(std::chrono::hours { 24 });
    auto const before = std::chrono::system_clock::now();
    REQUIRE(fixture.run(file->setTimes(NoChange {}, SetToNow {})).has_value());
    auto const after = std::chrono::system_clock::now();
    auto const now = fixture.run(file->stat());
    REQUIRE(now.has_value());
    CHECK(now->accessed == explicitTime);
    REQUIRE(now->modified.has_value());
    if (fixture.nowSource() == NowSource::InjectedClock)
        CHECK(*now->modified == core::fs::test::fileTimeOf(fixture.clock().now()));
    else
        CHECK(within(*now->modified, before, after));

    auto const atTime = FileTime { .seconds = 1'500'000'000, .nanoseconds = 0 };
    REQUIRE(fixture.run(fixture.root().setTimesAt(SymlinkPolicy::NoFollow, "f", atTime, atTime)).has_value());
    CHECK(fixture.run(file->stat())->modified == atTime);
}

TEST_CASE("contract: advise accepts every Advice", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    fixture.createFile("f", "content");
    auto file = fixture.run(
        fixture.root().openAt(SymlinkPolicy::NoFollow, "f", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE(file.has_value());
    using enum Advice;
    for (auto const advice: { Normal, Sequential, Random, WillNeed, DontNeed, NoReuse })
    {
        CAPTURE(static_cast<int>(advice));
        CHECK(outcomeOf(fixture.run((*file)->advise(0, 0, advice))) == std::nullopt);
        CHECK(outcomeOf(fixture.run((*file)->advise(2, 3, advice))) == std::nullopt);
    }
}

TEST_CASE("contract: a write at offset 0 after an append lands at 0", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto const file = createReadWrite(fixture, "f");
    REQUIRE(fixture.run(file->write(bytesOf("abcd"), 0)).value() == 4);
    REQUIRE(fixture.run(file->append(bytesOf("XY"))).value() == 2);
    REQUIRE(fixture.run(file->write(bytesOf("Z"), 0)).value() == 1);
    REQUIRE(fixture.run(file->append(bytesOf("!"))).value() == 1);
    CHECK(textOf(fixture.run(file->read(64, 0)).value().bytes) == "ZbcdXY!");
}

TEST_CASE("contract: a write changes the metadata hash, and nothing else does", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto const file = createReadWrite(fixture, "f");
    REQUIRE(fixture.run(file->write(bytesOf("content"), 0)).has_value());

    auto const first = fixture.run(file->metadataHash());
    REQUIRE(first.has_value());
    CHECK(fixture.run(file->metadataHash()).value() == *first);
    std::ignore = fixture.run(file->read(64, 0));
    CHECK(fixture.run(file->metadataHash()).value() == *first);

    // The write grows the file, so its size changes even where the clock's grain hides the time.
    REQUIRE(fixture.run(file->write(bytesOf("!"), 7)).has_value());
    auto const second = fixture.run(file->metadataHash());
    REQUIRE(second.has_value());
    CHECK(*second != *first);
}

TEST_CASE("contract: an open of a link without following changes neither the link nor its target",
          "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
        SKIP("this file system cannot create symbolic links");
    auto& root = fixture.root();
    fixture.createFile("target", "keep");
    REQUIRE(fixture.run(root.symlinkAt("target", "link")).has_value());

    for (auto const open: { OpenFlags::Create | OpenFlags::Truncate, OpenFlags::Truncate, OpenFlags::Create })
    {
        CAPTURE(static_cast<int>(open));
        CHECK(
            outcomeOf(fixture.run(root.openAt(SymlinkPolicy::NoFollow, "link", open, DescriptorFlags::Write)))
            == FsErrorCode::Loop);
    }
    CHECK(fixture.run(root.readlinkAt("link")).value() == "target");
    CHECK(fixture.run(root.statAt(SymlinkPolicy::NoFollow, "link"))->type == DescriptorType::SymbolicLink);
    CHECK(fixture.run(root.statAt(SymlinkPolicy::NoFollow, "target"))->size == 4);
}

TEST_CASE("contract: openAt creates, truncates and requires a directory as it is asked", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto& root = fixture.root();
    fixture.createFile("f", "content");
    REQUIRE(fixture.run(root.createDirectoryAt("d")).has_value());
    auto const nf = SymlinkPolicy::NoFollow;
    auto const sizeOf = [&](std::string name) {
        return fixture.run(root.statAt(nf, std::move(name)))->size;
    };

    SECTION("Create without Exclusive opens what exists and creates what does not")
    {
        REQUIRE(fixture.run(root.openAt(nf, "f", OpenFlags::Create, DescriptorFlags::Write)).has_value());
        CHECK(sizeOf("f") == 7);
        REQUIRE(fixture.run(root.openAt(nf, "g", OpenFlags::Create, DescriptorFlags::Write)).has_value());
        CHECK(fixture.run(root.statAt(nf, "g"))->type == DescriptorType::RegularFile);
        CHECK(sizeOf("g") == 0);
    }
    SECTION("Truncate empties what exists, Create | Truncate too")
    {
        auto file = fixture.run(
            root.openAt(nf, "f", OpenFlags::Truncate, DescriptorFlags::Read | DescriptorFlags::Write));
        REQUIRE(file.has_value());
        CHECK(sizeOf("f") == 0);
        CHECK(fixture.run((*file)->read(64, 0)).value().bytes.empty());
        fixture.createFile("h", "more content");
        REQUIRE(
            fixture.run(root.openAt(nf, "h", OpenFlags::Create | OpenFlags::Truncate, DescriptorFlags::Write))
                .has_value());
        CHECK(sizeOf("h") == 0);
        CHECK(outcomeOf(fixture.run(root.openAt(nf, "missing", OpenFlags::Truncate, DescriptorFlags::Write)))
              == FsErrorCode::NoEntry);
    }
    SECTION("Directory opens a directory and refuses anything else")
    {
        auto directory = fixture.run(root.openAt(nf, "d", OpenFlags::Directory, DescriptorFlags::Read));
        REQUIRE(directory.has_value());
        CHECK(fixture.run((*directory)->type()).value() == DescriptorType::Directory);
        CHECK(outcomeOf(fixture.run(root.openAt(nf, "f", OpenFlags::Directory, DescriptorFlags::Read)))
              == FsErrorCode::NotDirectory);
        CHECK(sizeOf("f") == 7);
    }
}

TEST_CASE("contract: a final link is followed where the call says so, and no further", "[fs][contract]")
{
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    if (fixture.requireSymlinks() == SymlinkSupport::Unavailable)
        SKIP("this file system cannot create symbolic links");
    auto& root = fixture.root();
    fixture.createFile("f", "content");
    REQUIRE(fixture.run(root.symlinkAt("f", "link")).has_value());
    REQUIRE(fixture.run(root.symlinkAt("link", "chain")).has_value());
    REQUIRE(fixture.run(root.symlinkAt("self", "self")).has_value());
    REQUIRE(fixture.run(root.symlinkAt("..", "up")).has_value());
    REQUIRE(fixture.run(root.symlinkAt("absent", "dangling")).has_value());
    auto const follow = SymlinkPolicy::Follow;

    CHECK(fixture.run(root.statAt(follow, "chain"))->type == DescriptorType::RegularFile);
    CHECK(fixture.run(root.statAt(follow, "chain"))->size == 7);
    CHECK(fixture.run(root.statAt(SymlinkPolicy::NoFollow, "chain"))->type == DescriptorType::SymbolicLink);
    auto file = fixture.run(root.openAt(follow, "chain", OpenFlags::None, DescriptorFlags::Read));
    REQUIRE(file.has_value());
    CHECK(textOf(fixture.run((*file)->read(64, 0)).value().bytes) == "content");
    CHECK(outcomeOf(fixture.run(root.statAt(follow, "self"))) == FsErrorCode::Loop);
    CHECK(outcomeOf(fixture.run(root.statAt(follow, "up"))) == FsErrorCode::NotPermitted);
    CHECK(outcomeOf(fixture.run(root.statAt(follow, "dangling"))) == FsErrorCode::NoEntry);

    // Create through a dangling link creates its target, as POSIX open(O_CREAT) does.
    REQUIRE(
        fixture.run(root.openAt(follow, "dangling", OpenFlags::Create, DescriptorFlags::Write)).has_value());
    CHECK(fixture.run(root.statAt(SymlinkPolicy::NoFollow, "absent"))->type == DescriptorType::RegularFile);
}

TEST_CASE("contract: a directory's descriptor reads no bytes", "[fs][contract]")
{
    // POSIX's pread answers EISDIR, Windows' ReadFile ERROR_INVALID_FUNCTION: both are IsDirectory.
    auto fixture = DescriptorFixture::create(GENERATE(FixtureKind::InMemory, FixtureKind::Native));
    auto& root = fixture.root();
    REQUIRE(fixture.run(root.createDirectoryAt("dir")).has_value());
    auto directory =
        fixture.run(root.openAt(SymlinkPolicy::NoFollow, "dir", OpenFlags::Directory, DescriptorFlags::Read));
    REQUIRE(directory.has_value());
    CHECK(outcomeOf(fixture.run((*directory)->read(16, 0))) == FsErrorCode::IsDirectory);
}
