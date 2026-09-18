// SPDX-License-Identifier: Apache-2.0
#include <core/cli/App.hpp>

#include <core/Environment.hpp>
#include <core/UserInfo.hpp>
#include <core/Utils.hpp>
#include <core/log/LogSink.hpp>
#include <core/log/LogStore.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>

#ifndef _WIN32
    #include <sys/ioctl.h>

    #include <unistd.h>
#endif

using std::bind;
using std::cout;
using std::exception;
using std::left;
using std::max;
using std::optional;
using std::setw;
using std::string;
using std::string_view;

using namespace std::string_view_literals;

namespace fs = std::filesystem;

namespace CLI = core::cli;

namespace
{
std::string operator*(std::string_view a, size_t n)
{
    std::string s;
    s.reserve(a.size() * n);
    for ([[maybe_unused]] auto const i: std::views::iota(size_t { 0 }, n))
        s += a;
    return s;
}

CLI::HelpDisplayStyle helpStyle()
{
    auto style = CLI::HelpDisplayStyle {};

    style.optionStyle = CLI::OptionStyle::Natural;

#ifndef _WIN32
    if (isatty(STDOUT_FILENO) == 0)
    {
        style.colors.reset();
        style.hyperlink = false;
    }
#endif

    return style;
}

unsigned screenWidth()
{
    constexpr auto DefaultWidth = 80u;

#ifndef _WIN32
    auto ws = winsize {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
        return ws.ws_col;
#endif

    return DefaultWidth;
}

/// @param env The environment to read the state-directory variables from.
/// @return The base directory this application's local state belongs under.
fs::path xdgStateHome(core::Environment const& env)
{
    if (auto const p = env.get("XDG_STATE_HOME"); p && !p->empty())
        return { *p };

#ifdef _WIN32
    if (auto const p = env.get("LOCALAPPDATA"); p && !p->empty())
        return { *p };
#else
    if (auto const home = core::userHomeDirectory(); !home.empty())
        return fs::path(home) / ".local" / "state";
#endif

    return fs::temp_directory_path();
}
} // namespace

namespace core::cli
{

namespace
{
    /// What App::instance() answers: the application constructed last and not yet destroyed.
    App* currentApp = nullptr;
} // namespace

App* App::instance() noexcept
{
    return currentApp;
}

App::App(Environment const& env,
         std::string appName,
         std::string appTitle,
         std::string appVersion,
         std::string appLicense):
    _environment { env },
    _appName { std::move(appName) },
    _appTitle { std::move(appTitle) },
    _appVersion { std::move(appVersion) },
    _appLicense { std::move(appLicense) },
    _localStateDir { xdgStateHome(env) / _appName }
{
    if (auto const logFilterString = env.get("LOG"))
    {
        core::log::configure(*logFilterString);
        customizeLogStoreOutput();
    }

    currentApp = this;

    link(_appName + ".help", bind(&App::helpAction, this));
    link(_appName + ".version", bind(&App::versionAction, this));
    link(_appName + ".license", bind(&App::licenseAction, this));
}

App::~App()
{
    currentApp = nullptr;
}

void App::link(std::string command, std::function<int()> handler)
{
    _handlers[std::move(command)] = std::move(handler);
}

void App::listDebugTags()
{
    auto& categories = core::log::get();
    std::ranges::sort(categories,
                      [](auto const& a, auto const& b) { return a.get().name() < b.get().name(); });

    auto const maxNameLength =
        std::accumulate(begin(categories), end(categories), size_t { 0 }, [&](auto acc, auto const& cat) {
            return !cat.get().visible() ? acc : max(acc, cat.get().name().size());
        });
    auto const column1Length = maxNameLength + 2;

    for (auto const& category: categories)
    {
        if (!category.get().visible())
            continue;

        // TODO: maybe have color assigned per category AND have that colored here then too?
        std::cout << left << setw(static_cast<int>(column1Length)) << category.get().name() << "; "
                  << category.get().description() << '\n';
    }
}

int App::helpAction()
{
    std::cout << CLI::helpText(_syntax.value(), helpStyle(), screenWidth());
    return EXIT_SUCCESS;
}

int App::licenseAction()
{
    auto const& store = about::store();
    auto const titleWidth = std::accumulate(
        store.begin(), store.end(), 0zu, [](size_t a, auto const& b) { return std::max(a, b.title.size()); });
    auto const licenseWidth = std::accumulate(store.begin(), store.end(), 0zu, [](size_t a, auto const& b) {
        return std::max(a, b.license.size());
    });
    auto const urlWidth = std::accumulate(
        store.begin(), store.end(), 0zu, [](size_t a, auto const& b) { return std::max(a, b.url.size()); });

    constexpr auto Horiz = "\u2550"sv;
    constexpr auto Vert = "\u2502"sv;
    constexpr auto Cross = "\u256A"sv;

    cout << '\n'
         << _appTitle << ' ' << _appVersion << '\n'
         << "License: " << _appLicense << '\n'
         << "\u2550"sv * (_appTitle.size() + _appVersion.size() + 1) << '\n'
         << '\n';

    cout << setw(static_cast<int>(titleWidth)) << "Project" << ' ' << Vert << ' '
         << setw(static_cast<int>(licenseWidth)) << "License" << ' ' << Vert << ' ' << "Project URL" << '\n';

    cout << Horiz * titleWidth << Horiz << Cross << Horiz << Horiz * licenseWidth << Horiz << Cross << Horiz
         << Horiz * urlWidth << '\n';

    for (auto const& project: about::store())
        cout << setw(static_cast<int>(titleWidth)) << project.title << ' ' << Vert << ' '
             << setw(static_cast<int>(licenseWidth)) << project.license << ' ' << Vert << ' ' << project.url
             << '\n';

    return EXIT_SUCCESS;
}

int App::versionAction()
{
    std::cout << std::format("{} {}\n\n", _appTitle, _appVersion);
    return EXIT_SUCCESS;
}

bool App::reparseParameters(int argc, char const* argv[])
{
    _syntax = parameterDefinition();
    optional<CLI::FlagStore> flagsOpt = CLI::parse(_syntax.value(), argc, argv);
    if (!flagsOpt.has_value())
        return false;
    _flags = std::move(flagsOpt.value());
    return true;
}

bool App::parseParametersForTesting(int argc, char const* argv[])
{
    return reparseParameters(argc, argv);
}

int App::run(int argc, char const* argv[])
{
    try
    {
        customizeLogStoreOutput();

        // Kept before parsing consumes it: a verb that must relaunch this binary with this
        // configuration replays these tokens verbatim (see commandLine()).
        _commandLine.assign(argv, argv + argc);

        _syntax = parameterDefinition();

        optional<CLI::FlagStore> flagsOpt = CLI::parse(_syntax.value(), argc, argv);
        if (!flagsOpt.has_value())
        {
            std::cerr << "Failed to parse command line parameters.\n";
            return EXIT_FAILURE;
        }
        _flags = std::move(flagsOpt.value());

        // std::cout << std::format("Flags: {}\n", parameters().values.size());
        // for (auto const& [k, v]: parameters().values)
        //     std::cout << std::format(" - {}: {}\n", k, v);

        for (auto const& [name, handler]: _handlers)
            if (parameters().get<bool>(name))
                return handler();

        std::cerr << "Usage error." << '\n';
        return EXIT_FAILURE;
    }
    catch (exception const& e)
    {
        std::cerr << std::format("Unhandled error caught. {}", e.what()) << '\n';
        return EXIT_FAILURE;
    }
}

std::expected<void, std::string> App::installLogging(std::string const& optionPrefix, bool showProcessId)
{
    auto const filter = parameters().get<std::string>(optionPrefix + ".log");
    auto output = core::log::ScopedOutput::create({
        .filter = filter,
        .file = core::log::parseLogFileSpec(parameters().get<std::string>(optionPrefix + ".log-file")),
        .showProcessId = showProcessId,
    });
    if (!output)
        return std::unexpected(output.error());

    // A pattern matching nothing is nearly always a typo, and its symptom — no output — looks
    // exactly like "the thing you asked about never happened".
    for (auto const& unmatched: core::log::unmatchedFilters(filter))
        std::cerr << std::format("{}: --log '{}' matches no known tag (see `{} list-debug-tags`).\n",
                                 _appName,
                                 unmatched,
                                 _appName);

    _logOutput = std::move(*output);
    return {};
}

void App::customizeLogStoreOutput()
{
    core::log::Sink::console().setEnabled(true);

    // console() writes to std::cout, so STDOUT is the right stream to ask about here.
    // (A destination that writes elsewhere must gate on ITS stream — see core::log::ScopedOutput.)
    static bool const colorized =
#ifndef _WIN32
        isatty(STDOUT_FILENO) != 0;
#else
        true;
#endif

    // The historical console shape: timestamped standard lines, and a bare `[error]` tag with
    // no timestamp for errors. Destinations that want the process id (or a timestamp on error
    // lines) build their own options; the layout itself is single-sourced in logsink.cpp.
    core::log::setFormatter(core::log::makeStandardFormatter({ .colorize = colorized }));
    core::log::errorLog.setFormatter(
        core::log::makeErrorFormatter({ .colorize = colorized, .showTimestamp = false }));
}

} // namespace core::cli
