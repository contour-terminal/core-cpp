// SPDX-License-Identifier: Apache-2.0
#include <core/log/LogSink.hpp>
#include <core/log/LogStore.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Single-threaded WebAssembly has no threads to start (Part I §1).
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    #define CORE_CPP_TEST_THREADS 1
    #include <ranges>
    #include <thread>
#else
    #define CORE_CPP_TEST_THREADS 0
#endif

#include <sys/stat.h>

#include <fcntl.h>

#ifdef _WIN32
    #include <io.h>
    #include <process.h>
    #include <share.h>
#else
    #include <unistd.h>
#endif

using namespace std::string_view_literals;

namespace
{
/// A category that exists only for the duration of one test.
///
/// core::log::Category asserts name uniqueness process-wide and deregisters itself on
/// destruction, so a function-local category is the only way to build messages in a test
/// without colliding with the real ones.
struct TestCategory
{
    core::log::Category value;

    explicit TestCategory(std::string_view name):
        value { name, "Test-only category.", core::log::Category::State::Enabled }
    {
    }
};

/// @return The current process id, as the `[PID]` field prints it.
[[nodiscard]] int processId() noexcept
{
#ifdef _WIN32
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

/// @return What is left to read of @p stream.
///
/// Through a string stream rather than std::istreambuf_iterator: GCC 14 at -O2 inlines that
/// iterator's reads into the caller and reports a possible null dereference inside libstdc++'s
/// streambuf (-Wnull-dereference), which no caller can act on.
[[nodiscard]] std::string contentsOf(std::istream& stream)
{
    auto buffer = std::ostringstream {};
    buffer << stream.rdbuf();
    return buffer.str();
}

/// A scratch log-file path, deleted when the test ends.
///
/// Declare it FIRST in the test: the deletion happens in the destructor, so it runs after every
/// sink and reader declared below it has closed. Windows refuses to delete a file any handle still
/// holds open — unlike POSIX, where unlinking an open file is fine — so a `remove` written inline
/// after an `ifstream` throws there while passing everywhere else.
///
/// Deletion is the non-throwing overload, because a destructor must not throw and a leftover file
/// in the temp directory is not worth failing a test over.
class ScratchLog
{
  public:
    explicit ScratchLog(std::string_view stem):
        _path(std::filesystem::temp_directory_path() / std::format("core-cpp-logsink-{}.log", stem))
    {
        remove(); // a leftover from an earlier crashed run would make an append test lie
    }

    ~ScratchLog() { remove(); }

    ScratchLog(ScratchLog const&) = delete;
    ScratchLog& operator=(ScratchLog const&) = delete;
    ScratchLog(ScratchLog&&) = delete;
    ScratchLog& operator=(ScratchLog&&) = delete;

    [[nodiscard]] std::filesystem::path const& path() const noexcept { return _path; }

  private:
    void remove() const noexcept
    {
        auto ec = std::error_code {};
        std::filesystem::remove(_path, ec);
    }

    std::filesystem::path _path;
};

/// Puts a category's enablement back when the test leaves, however it leaves.
///
/// A plain `disable()` with a re-enable at the end of the body is not enough: a failing REQUIRE
/// unwinds past it, and the category stays off for every later case in the binary.
class ScopedCategoryState
{
  public:
    explicit ScopedCategoryState(core::log::Category& category):
        _category { category }, _wasEnabled { category.isEnabled() }
    {
    }

    ~ScopedCategoryState() { _category.enable(_wasEnabled); }

    ScopedCategoryState(ScopedCategoryState const&) = delete;
    ScopedCategoryState& operator=(ScopedCategoryState const&) = delete;
    ScopedCategoryState(ScopedCategoryState&&) = delete;
    ScopedCategoryState& operator=(ScopedCategoryState&&) = delete;

  private:
    core::log::Category& _category;
    bool _wasEnabled;
};

/// Points one of this process's standard descriptors at a file for this object's lifetime.
class ScopedRedirect
{
  public:
    ScopedRedirect(int fd, std::filesystem::path const& path): _fd { fd }
    {
        auto const target = openForWriting(path);
        if (target == -1)
            return;
        _saved = duplicate(fd);
        if (_saved != -1)
            duplicate2(target, fd);
        closeDescriptor(target);
    }

    ~ScopedRedirect()
    {
        if (_saved != -1)
        {
            duplicate2(_saved, _fd);
            closeDescriptor(_saved);
        }
    }

    ScopedRedirect(ScopedRedirect const&) = delete;
    ScopedRedirect& operator=(ScopedRedirect const&) = delete;
    ScopedRedirect(ScopedRedirect&&) = delete;
    ScopedRedirect& operator=(ScopedRedirect&&) = delete;

    /// @return Whether the descriptor could actually be redirected.
    [[nodiscard]] bool isActive() const noexcept { return _saved != -1; }

  private:
    /// @return A descriptor for @p path, truncated, or -1.
    [[nodiscard]] static int openForWriting(std::filesystem::path const& path) noexcept
    {
#ifdef _WIN32
        auto fd = -1;
        (void) ::_wsopen_s(
            &fd, path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC, _SH_DENYNO, _S_IREAD | _S_IWRITE);
        return fd;
#else
        return ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    }

    [[nodiscard]] static int duplicate(int fd) noexcept
    {
#ifdef _WIN32
        return ::_dup(fd);
#else
        return ::dup(fd);
#endif
    }

    static void duplicate2(int from, int to) noexcept
    {
#ifdef _WIN32
        (void) ::_dup2(from, to);
#else
        (void) ::dup2(from, to);
#endif
    }

    static void closeDescriptor(int fd) noexcept
    {
#ifdef _WIN32
        (void) ::_close(fd);
#else
        (void) ::close(fd);
#endif
    }

    int _fd;
    int _saved = -1;
};
} // namespace

TEST_CASE("parseLogFileSpec maps the standard-error spellings onto nullopt", "[log][logsink]")
{
    CHECK_FALSE(core::log::parseLogFileSpec("").has_value());
    CHECK_FALSE(core::log::parseLogFileSpec("-").has_value());
    CHECK(core::log::parseLogFileSpec("/tmp/core-cpp.log") == std::filesystem::path { "/tmp/core-cpp.log" });
}

TEST_CASE("the standard formatter lays a line out as configured", "[log][logsink]")
{
    auto category = TestCategory { "test.formatter" };
    auto capture = core::log::ScopedCapture { "test.formatter" };

    SECTION("uncoloured output carries no escape sequences")
    {
        category.value.setFormatter(core::log::makeStandardFormatter({ .colorize = false }));
        category.value()("hello");
        CHECK_FALSE(capture.contains("\033"));
        CHECK(capture.contains("[test.formatter]"));
        CHECK(capture.contains("hello"));
    }

    SECTION("colourised output does")
    {
        category.value.setFormatter(core::log::makeStandardFormatter({ .colorize = true }));
        category.value()("hello");
        CHECK(capture.contains("\033["));
    }

    SECTION("the process id appears only when asked for")
    {
        category.value.setFormatter(
            core::log::makeStandardFormatter({ .colorize = false, .showProcessId = true }));
        category.value()("hello");
        CHECK(capture.contains(std::format("[{}]", processId())));
    }

    SECTION("the timestamp can be suppressed")
    {
        category.value.setFormatter(
            core::log::makeStandardFormatter({ .colorize = false, .showTimestamp = false }));
        category.value()("hello");
        CHECK(capture.text().starts_with("[test.formatter] hello"));
    }

    SECTION("continuation lines are indented and carry no repeated tag")
    {
        category.value.setFormatter(
            core::log::makeStandardFormatter({ .colorize = false, .showTimestamp = false }));
        category.value()("first\nsecond");
        auto const lines = capture.lines();
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "[test.formatter] first");
        CHECK(lines[1] == "        second");
    }
}

TEST_CASE("the error formatter tags its lines so they stand out", "[log][logsink]")
{
    auto category = TestCategory { "test.errorformat" };
    auto capture = core::log::ScopedCapture { "test.errorformat" };

    category.value.setFormatter(core::log::makeErrorFormatter({ .colorize = false, .showTimestamp = false }));
    category.value()("it broke");
    CHECK(capture.text() == "[error] it broke\n");
}

TEST_CASE("unmatchedFilters names filter patterns that select nothing", "[log][logsink]")
{
    auto category = TestCategory { "test.filters" };

    CHECK(core::log::unmatchedFilters("test.filters").empty());
    CHECK(core::log::unmatchedFilters("test.*").empty());
    CHECK(core::log::unmatchedFilters("all").empty());
    CHECK(core::log::unmatchedFilters("").empty());
    CHECK(core::log::unmatchedFilters("test.filtres") == std::vector<std::string> { "test.filtres" });
    CHECK(core::log::unmatchedFilters("test.filters,nope.*") == std::vector<std::string> { "nope.*" });
}

TEST_CASE("ScopedOutput writes to a file without escape sequences", "[log][logsink]")
{
    auto const log = ScratchLog { "file" };
    auto category = TestCategory { "test.filesink" };

    {
        auto output = core::log::ScopedOutput::create({ .file = log.path() });
        REQUIRE(output.has_value());
        category.value()("to the file");
    }

    auto stream = std::ifstream { log.path() };
    REQUIRE(stream.is_open());
    auto const contents = contentsOf(stream);
    CHECK(contents.contains("to the file"));
    CHECK(contents.contains("[test.filesink]"));
    // A file must never receive SGR escapes, whatever the terminal the daemon was started from.
    CHECK_FALSE(contents.contains('\033'));
}

TEST_CASE("ScopedOutput appends rather than truncating", "[log][logsink]")
{
    // A daemon restarted against the same --log-file must not erase the evidence of why the
    // previous run died.
    auto const log = ScratchLog { "append" };
    auto category = TestCategory { "test.appendsink" };

    for (auto const* const text: { "first run", "second run" })
    {
        auto output = core::log::ScopedOutput::create({ .file = log.path() });
        REQUIRE(output.has_value());
        category.value()("{}", text);
    }

    auto stream = std::ifstream { log.path() };
    auto const contents = contentsOf(stream);
    CHECK(contents.contains("first run"));
    CHECK(contents.contains("second run"));
}

TEST_CASE("ScopedOutput reports an unopenable log file", "[log][logsink]")
{
    // A DIRECTORY is the only destination portably guaranteed to refuse an ofstream: POSIX answers
    // EISDIR, Windows EACCES. A path under an unwritable directory is not — create() creates the
    // parent first, and an absolute POSIX path like "/proc/nope/x.log" is a perfectly valid
    // DRIVE-RELATIVE path on Windows, so create_directories() happily makes it and the open
    // succeeds. That is exactly how this test used to fail on Windows CI alone.
    auto const output = core::log::ScopedOutput::create({ .file = std::filesystem::temp_directory_path() });
    REQUIRE_FALSE(output.has_value());
    CHECK(output.error().contains("cannot open log file"));
}

TEST_CASE("ScopedOutput restores the previous sink", "[log][logsink]")
{
    // The regression test for Category's reference_wrapper hazard: a category left pointing at
    // a destroyed sink corrupts every later log call in the process.
    auto category = TestCategory { "test.restore" };
    auto const* const before = &category.value.sink();

    auto const log = ScratchLog { "restore" };
    {
        auto output = core::log::ScopedOutput::create({ .file = log.path() });
        REQUIRE(output.has_value());
        CHECK(&category.value.sink() != before);
    }

    CHECK(&category.value.sink() == before);
}

TEST_CASE("ScopedOutput leaves an empty filter alone", "[log][logsink]")
{
    // configure("") matches no pattern and would therefore DISABLE every category, `error`
    // included. An empty --log must mean "keep whatever $LOG set", never "log nothing".
    auto category = TestCategory { "test.emptyfilter" };
    REQUIRE(category.value.isEnabled());

    auto output = core::log::ScopedOutput::create({ .filter = "" });
    REQUIRE(output.has_value());
    CHECK(category.value.isEnabled());
    CHECK(core::log::errorLog.isEnabled());
}

#if CORE_CPP_TEST_THREADS
TEST_CASE("ScopedOutput serialises concurrent writers", "[log][logsink]")
{
    // A server logs from its event loop, a signal thread and its workers at once, while
    // Sink itself does no locking at all. Without the writer's mutex, lines tear.
    static constexpr auto ThreadCount = 4;
    static constexpr auto LinesPerThread = 200;

    auto const log = ScratchLog { "threads" };
    auto category = TestCategory { "test.threads" };

    {
        auto output = core::log::ScopedOutput::create({ .file = log.path() });
        REQUIRE(output.has_value());

        auto writers = std::vector<std::thread> {};
        for (auto const worker: std::views::iota(0, ThreadCount))
            writers.emplace_back([&category, worker] {
                for (auto const line: std::views::iota(0, LinesPerThread))
                    category.value()("worker {} line {}", worker, line);
            });
        for (auto& writer: writers)
            writer.join();
    }

    auto stream = std::ifstream { log.path() };
    auto lines = std::vector<std::string> {};
    auto text = std::string {};
    while (std::getline(stream, text))
        lines.push_back(text);

    CHECK(lines.size() == std::size_t { ThreadCount } * LinesPerThread);
    // Every line intact means no writer interleaved inside another's bytes.
    CHECK(std::ranges::all_of(lines, [](auto const& line) {
        return line.contains("[test.threads] worker ") && line.contains(" line ");
    }));
}
#endif

TEST_CASE("ScopedCapture enables and restores what it captures", "[log][logsink]")
{
    auto category = core::log::Category { "test.capture", "Test-only category." };
    REQUIRE_FALSE(category.isEnabled());
    auto const* const before = &category.sink();

    {
        auto capture = core::log::ScopedCapture { "test.capture" };
        CHECK(category.isEnabled()); // capturing a category implies enabling it
        category()("recorded");
        CHECK(capture.contains("recorded"));
        CHECK(capture.count("recorded") == 1);
    }

    CHECK_FALSE(category.isEnabled());
    CHECK(&category.sink() == before);
}

TEST_CASE("configure's prefix match is bounded by the category name", "[log][logsink]")
{
    // Regression: the wildcard branch used a three-iterator std::equal, which bounds only the
    // PATTERN range. Any pattern longer than a registered category's name — "vthost.*" is 7
    // characters against the built-in "error" at 5 — read past the end of that name. ASan
    // caught it the first time a real `--log=vthost.*` ran.
    auto category = TestCategory { "test.prefixbound" };

    core::log::configure("averyveryverylongprefixthatnocategoryhas.*");
    CHECK_FALSE(category.value.isEnabled());

    core::log::configure("test.*");
    CHECK(category.value.isEnabled());

    // A pattern that is a strict prefix of another category's name must not match it whole.
    core::log::configure("test.prefixboundandmore*");
    CHECK_FALSE(category.value.isEnabled());

    core::log::configure("all"); // leave the process in a sane state for later tests
    core::log::configure("error");
}

TEST_CASE("an explicit filter never silences the error category", "[log][logsink]")
{
    // core::log::configure is a SELECTION: it disables everything the filter does not name.
    // Left alone, `--log vthost.trace.proto` would switch off the very failure lines an
    // operator turned logging on to see. Caught by running a real daemon, not by a unit test —
    // hence this one.
    auto category = TestCategory { "test.filterkeepserror" };

    // errorLog is process-wide and this case turns it off on purpose; the guard puts it back
    // even when a REQUIRE below unwinds out of the body.
    auto const restoreErrorLog = ScopedCategoryState { core::log::errorLog };
    core::log::errorLog.disable();

    auto output = core::log::ScopedOutput::create({ .filter = "test.filterkeepserror" });
    REQUIRE(output.has_value());

    CHECK(category.value.isEnabled());
    CHECK(core::log::errorLog.isEnabled());
}

// The Windows half of this answered `true` unconditionally, so a redirected stream was
// colourised -- against the header's contract, and into whatever file or pipe was reading.
TEST_CASE("a redirected standard stream is not a terminal", "[log][logsink]")
{
    auto const scratch = ScratchLog { "redirect" };

    SECTION("standard output")
    {
        auto const redirect = ScopedRedirect { 1, scratch.path() };
        if (!redirect.isActive())
            SKIP("standard output could not be redirected");
        CHECK(!core::log::isStdOutTerminal());
    }

    SECTION("standard error")
    {
        auto const redirect = ScopedRedirect { 2, scratch.path() };
        if (!redirect.isActive())
            SKIP("standard error could not be redirected");
        CHECK(!core::log::isStdErrTerminal());
    }
}
