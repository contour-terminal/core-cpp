# Task A9: Phase A gate, pass 1 findings (base, log, cli)

The Phase A gate ran `/code-review` over `4abb140..6dda244` (Tasks A1, A2, A3, A3b: the CMake framework, docs and CI, and `core::base`, `core::log`, `core::cli`). It found 15 defects. All are in files byte-identical between that range and HEAD, so all still stand. Every one came in with contour's crispy and is ours to fix now: contour, endo and tuidu all link these modules, and `core::cli` is what draws their `--help`.

Fix each one **test-first**: a case that fails before the fix, in the module's existing `*_test.cpp` next to the source. Two gaps are worth closing while you are there: there is no `Flags_test.cpp` at all, and `Escape` has no round-trip test.

## Crash and corruption

1. **`src/core/cli/CLI.cpp:611` — unsigned underflow makes `--help` segfault.**
   `wordWrapped()` computes `margin - cursor + 1` in unsigned arithmetic. `printOptions()` sets `cursor = columnWidth + 1`, so in an 80-column terminal any option whose rendered text exceeds 65 characters — or any terminal narrower than `columnWidth`, or `ws_col == 0` — underflows. `if (rightMargin <= 0)` is then dead for an unsigned type, `i = rightMargin - 1` is about 4294967295, and `text[i]` reads far out of bounds.
2. **`src/core/cli/CLI.cpp:841` — a verbatim placeholder longer than the longest option wraps to a ~4 GB allocation.**
   `columnWidth - leftSize` underflows; the `assert` above it is compiled out under NDEBUG, and `spaces(n)` becomes `std::string(4294967281, ' ')`. A `Command` with a verbatim placeholder and no options reaches it.
3. **`src/core/cli/CLI.cpp:597` — help text starting with a newline reads `text[SIZE_MAX]`.**
   `auto i = linefeed - 1` on a `size_t` with `linefeed == 0`.
4. **`src/core/Utils.hpp:303` — `splitKeyValuePairs()` reads past the view.**
   The last segment is built with the length-less `std::string_view(text.data() + iBeg)`, which calls `strlen` although a `string_view` carries no NUL guarantee. `text.substr(iBeg)` is the correct spelling. The tests only pass literals, so they never catch it.
5. **`src/core/Utils.cpp:32` — `threadName()` on Windows does `resize(len - 1)` with `len == 0` on failure**, throwing `length_error` and leaking the buffer, because the `LocalFree` on the next line never runs. Guard `len <= 0`, free, return empty.
6. **`src/core/cli/App.cpp:77` — `screenWidth()` accepts `ws_col == 0`**, which is common under a pty with no size set, and hands it to `wordWrapped()` as the margin, triggering finding 1. Clamp to the default width.

## Wrong answers

7. **`src/core/Flags.hpp:84` — `operator&=` clears the flag instead of intersecting.**
   It calls `disable(flag)`, so `f &= X::A` leaves everything except A, while `f = f & Flags{X::A}` yields A. It is the opposite of `operator&`. There is no `Flags_test.cpp`: write one, covering every operator against its non-compound form.
8. **`src/core/Base64.hpp:159` — `decodeLength()`'s scan never stops.**
   It compares the table entry against `std::size(index)` (256) rather than the 64 sentinel, so every entry passes and the length comes from the whole input, not the base64 prefix. Decoding a short payload followed by junk allocates for the junk. The intended test is the one `decode()` itself uses at `:191`.
9. **`src/core/Escape.hpp:35` — escape and unescape do not round-trip, and the printable range is off by one.**
   `ch < 0x7E` escapes `~`. `escape(0x41, NumericEscape::Octal)` emits `\101`, which `unescape()` cannot decode (its Escape state only knows `\0` followed by exactly two digits), and `escape('"')` emits `\"`, which `unescape()` re-emits as `\"`. Add the round-trip test for both numeric styles and for every character class.
10. **`src/core/FNV.hpp:61` — the trivially-copyable overload hashes padding bytes** through a `reinterpret_cast`, so two objects with equal members can hash differently, and the `constexpr` can never be evaluated at compile time. Restrict it to `std::has_unique_object_representations_v`, or hash members.
11. **`src/core/Utils.hpp:550` — `toLower()` and `toUpper()` pass a plain `char` to `tolower()` and `toupper()`**, which is undefined for any byte with the high bit set. `cli::about::registerProjects()` sorts titles through it, so a UTF-8 title reaches it. `CLI.cpp:555` passes a `char` to `isalpha()` the same way. Cast to `unsigned char` at each call.
12. **`src/core/Utils.hpp:582` — `readFileAsString()` returns trailing NULs and cannot open a non-ASCII path.**
    It sizes from `file_size()`, reads in text mode (so CRLF translation delivers fewer bytes and sets failbit), never consults `gcount()`, and narrows the path through `path.string()`. Pass the `std::filesystem::path` directly, open binary or truncate to `gcount()`.

## Silent misbehaviour

13. **`src/core/cli/App.cpp:293` — `installLogging()` called twice silently sends every later log line to stdout.**
    `_logOutput = std::move(*output)` destroys the previous `ScopedOutput` *after* the new one installed itself, and the old destructor restores every category to the sink it snapshotted. Reset before creating the replacement, or refuse a second call. `reparseParameters()` exists precisely for the re-entering case.
14. **`src/core/log/LogSink.cpp:277` — `isStdErrTty()` returns `true` unconditionally on Windows**, so a redirected stream receives SGR escapes, against the header's own contract. `App.cpp`'s `helpStyle()` and `customizeLogStoreOutput()` do the same for stdout. It is also an `#ifdef` in logic, which `.agent/rules/platform.md` forbids: use the platform seam (`_isatty(_fileno(...))` behind the existing injection), not a hard-coded branch.
15. **`src/core/cli/App.cpp:224` — `cli::parse()` throws although its contract says it returns `std::nullopt`**, and `reparseParameters()` (documented "false on failure") does not catch, so an exception escapes a function whose contract is a bool. `App::run()` catches; these two do not.
    - core-cpp#13 will convert this API to `std::expected` at the end of the plan. Do **not** do that here. Make the current contract true: catch where the documented return says failure is a value, or correct the documentation. Say in the report which you chose and why.

## Lower severity, fix what is cheap

The review also listed: a duplicated default assignment (`CLI.cpp:383-384`) and a re-set value (`:428-429`); a dead `if (com.children.empty())` inside the `else` of the same test (`:977`); `printOption(option, nullopt, style)` re-rendered two or three times per option per help run; `decode()`'s index lambda copying a 256-byte table per call; `Times2D::operator[]` returning only the inner element although `value_type` is a tuple; `eachElement<T>()` computing `end()` as `static_cast<T>(static_cast<int>(max) + 1)`, which yields `begin()` for `uint32_t` and signed overflow for `int`; `joinHumanReadableQuoted`'s defaulted `Separator sep = ", "` being unusable because `Separator` is deducible; `App::listDebugTags()` sorting the process-wide category registry in place, against the construction order `core::log::get()` documents; `layOut()`'s fixed 8-space continuation indent not lining up once a prefix is present; and `LogSink_test.cpp`'s "an explicit filter never silences the error category" disabling the global `errorLog` with no RAII restore, so a failing `REQUIRE` leaves it off for every later case in the binary.

Fix the ones that are a line or two and clearly right — the test's missing restore, the dead branch, the duplicate assignments, `eachElement`, the table copy. For anything needing an API decision (`Times2D::operator[]`, `joinHumanReadableQuoted`), report it instead and the controller will rule.

## Then

- The presets the constraints name, on Windows and WSL, including the sanitizer presets: findings 1, 3 and 4 should show as ASan reports before their fixes.
- `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- CHANGELOG entries under Fixed, and a provenance note per file saying it now diverges from contour's crispy and why.
- Push, watch CI and portability to green.
- Write your work to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A9-report.md` with RED/GREEN per finding.
