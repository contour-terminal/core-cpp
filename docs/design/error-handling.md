# Error handling

- **A fallible operation returns `std::expected<T, E>`.** Chain results with `and_then`,
  `or_else`, `transform` and `transform_error` rather than nesting `if`s.
- **Each module has its own error type**, added as the need arises: `core::net::NetError` (with
  `NetErrorCode`), `core::platform::PlatformError`.
- **The one exception type is `core::coro::OperationCancelled`.** A coroutine throws it when its
  own stop token cancels it, so cancellation unwinds a whole chain of coroutines at once.
- **A cancellation that comes from the resource is a value.** Closing a socket, calling
  `cancelRead()` or closing a listener completes a pending operation with
  `NetErrorCode::Cancelled`, not an exception.
- **If data already arrived, the data wins** over a concurrent cancellation.
- **A precondition violation is an assertion, not an error code**: there is no result it could
  return that would be true, so an error code would make every caller handle a case that cannot
  legitimately happen.
- **The reason is captured where it happens.** An `errno`, an `error_code` or a Windows error is
  available only at the point of failure; a `bool` result loses it.

The design is Part I §2 of the
[design spec](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md);
the rules are in
[`design-principles.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/design-principles.md).
