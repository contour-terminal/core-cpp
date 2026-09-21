# SPDX-License-Identifier: Apache-2.0

# The WebAssembly smoke: a program that is not a Catch2 binary, because what it exercises is not a
# case — it is the host's own event loop, and the only way to give it one is to let node be it.
#
# It needs `-sASYNCIFY`, which rewrites the module so a C++ call stack can be unwound at
# `emscripten_sleep` and rewound when the host comes back. That flag belongs to THIS program and
# nowhere else: it costs code size and start-up time on every function it instruments, and
# core-cpp adds no link flag to its consumers (Part I §3). A consumer that wants to block the way
# this does adds it in their own build, which `tests/consumer-wasm` shows.
if(NOT EMSCRIPTEN)
    return()
endif()

# Guarded, although `add_subdirectory(tests)` is already inside `if(CORE_CPP_TESTING)`: a bare
# add_executable()/add_test() pair is what escapes that gate when the guard moves, and this file
# then registers a test naming a binary a consumer's build never made -- "Not Run", in somebody
# else's ctest. Task B4 paid for that once.
if(NOT CORE_CPP_TESTING)
    return()
endif()

add_executable(core-cpp-hostdriven-timer-smoke HostDrivenTimer_smoke.cpp)
target_link_libraries(core-cpp-hostdriven-timer-smoke PRIVATE core::net)
core_cpp_apply_toolchain(core-cpp-hostdriven-timer-smoke)
target_link_options(core-cpp-hostdriven-timer-smoke PRIVATE -sASYNCIFY)

# ctest runs it through CMAKE_CROSSCOMPILING_EMULATOR, which the emscripten preset sets to node,
# exactly as it runs every Catch2 binary here.
#
# **PASS_REGULAR_EXPRESSION, because this program's exit status does not survive the host.**
# Measured rather than reasoned: with the program's deadline moved past its own bound, it printed
#
#   FAIL host-driven-timer: after 2000 ms the coroutine delay did NOT fire ...
#
# and node exited **0**. A loop with an armed deadline always leaves a pending
# `emscripten_async_call` behind -- that call IS how the host is asked for its next turn -- and
# Emscripten's `safeSetTimeout` takes a runtime keepalive per pending timer. `main` returning is
# then an IMPLICIT exit while the runtime is kept alive, which records the status and returns
# without publishing it; node drains its event loop and exits 0.
#
# Two fixes were tried and rejected before this one. `-sEXIT_RUNTIME=1` changes nothing: what the
# exit path reads is the keepalive COUNTER, not `noExitRuntime`. `emscripten_force_exit()` does
# publish the status (the same run then exited 1) but zeroes that counter, so the pending timer's
# later `runtimeKeepalivePop` trips an assertion and node dies with exit 7 and a JavaScript
# stack -- which turns a legible failure into an abort, and would do the same to a run that had
# already succeeded.
#
# So the program says what happened and ctest reads that. Setting PASS_REGULAR_EXPRESSION makes
# ctest ignore the return code entirely, which is the right trade here and the reason FAIL... is
# set beside it: a crash before either line is printed matches neither, and fails.
#
# TIMEOUT 120 rather than the 300 core_cpp_add_test would give: the program bounds its OWN wait at
# two seconds and names the arm that did not fire, so ctest's bound is only the backstop for a node
# that never starts.
add_test(NAME core-cpp.hostdriven-timer-smoke COMMAND core-cpp-hostdriven-timer-smoke)
set_tests_properties(core-cpp.hostdriven-timer-smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "ok host-driven-timer: a coroutine delay and a DeadlineTimer both fired"
    FAIL_REGULAR_EXPRESSION "FAIL host-driven-timer:"
    LABELS "core-cpp;net"
    TIMEOUT 120)
