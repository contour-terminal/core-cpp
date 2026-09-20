// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Concepts shared across @c core::async awaitables and coroutine promises.
///
/// It sits below @c Cancellation.hpp rather than beside it: @c HasStopToken is the contract every
/// templated `await_suspend` in this module reads the awaiting promise through, so the header that
/// states it may depend on nothing of the module but @c StopToken.

#include <core/async/StopToken.hpp>

#include <concepts>
#include <coroutine>

namespace core::async
{

/// Satisfied by a type that can drive a `co_await` expression directly, i.e. it
/// exposes the awaiter interface (`await_ready`, `await_suspend`, `await_resume`).
///
/// `await_suspend` is intentionally not constrained here because it may legally
/// return `void`, `bool`, or a `std::coroutine_handle<>` (symmetric transfer).
/// @tparam A The candidate awaiter type.
template <typename A>
concept Awaiter = requires(A awaiter, std::coroutine_handle<> continuation) {
    { awaiter.await_ready() } -> std::convertible_to<bool>;
    awaiter.await_suspend(continuation);
    awaiter.await_resume();
};

/// Satisfied by a coroutine promise that carries a cancellation @c StopToken.
///
/// Runtime awaitables read the awaiting coroutine's token through this interface
/// in their templated `await_suspend`, so cancellation propagates down a
/// structured chain of `co_await`s without threading a token through every call.
/// @tparam P The candidate promise type.
template <typename P>
concept HasStopToken = requires(P& promise) {
    { promise.stopToken() } -> std::convertible_to<StopToken>;
};

/// Satisfied by a coroutine promise that carries the root of the await chain it belongs to, for
/// the case where that chain belongs to nobody.
///
/// It is the second thing a templated `await_suspend` reads the awaiting promise through
/// (@c HasStopToken being the first): `core::async::detail::unownedRootOf` answers *may an
/// executor free what it is holding* from it, and a promise that does not carry the answer says
/// "not mine", which is what a coroutine type this module does not know should say.
/// @tparam P The candidate promise type.
template <typename P>
concept CarriesUnownedRoot = requires(P& promise) {
    { promise.unownedRoot } -> std::convertible_to<std::coroutine_handle<>>;
};

} // namespace core::async
