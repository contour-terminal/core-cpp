// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Test-only helpers the `core::http` cases share: raw wire text written to a socket, a body read
/// to its end, and a flow run on a hand-driven loop until it has an answer. Test-only: in no
/// `FILE_SET`, so never installed.

#include <core/async/Task.hpp>
#include <core/http/Body.hpp>
#include <core/http/Message.hpp>
#include <core/net/ISocket.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::http::test
{

/// Whether this process runs under wine rather than on Windows or a POSIX system. Defined per
/// platform (`posix/HttpTestSupport.cpp`, `windows/HttpTestSupport.cpp`); only a binary that links
/// one of them may call it. A case uses it to accept a behaviour wine has and no native kernel
/// does, and to fail where the same behaviour shows natively. A @c bool because the name asks it.
[[nodiscard]] bool runsUnderWine() noexcept;

/// @param text The bytes to view.
/// @return @p text as the byte span a socket write takes.
[[nodiscard]] inline std::span<std::byte const> bytesOf(std::string_view text) noexcept
{
    return { reinterpret_cast<std::byte const*>(text.data()), text.size() };
}

/// @param bytes Bytes a socket or a body reader handed out.
/// @return The same bytes as text.
[[nodiscard]] inline std::string textOf(std::span<std::byte const> bytes)
{
    return { reinterpret_cast<char const*>(bytes.data()), bytes.size() };
}

/// Writes @p text to @p socket in full, then closes it: a peer that sends exactly these bytes.
/// @param socket The writer; must outlive the returned task.
/// @param text The wire bytes, owned by the frame.
inline async::Task<void> writeAndClose(net::ISocket* socket, std::string text)
{
    if (!text.empty())
        static_cast<void>(co_await socket->write(bytesOf(text)));
    socket->close();
}

/// Writes @p text to @p socket in full and leaves it open: a peer that then falls silent.
/// @param socket The writer; must outlive the returned task.
/// @param text The wire bytes, owned by the frame.
inline async::Task<void> writeOnly(net::ISocket* socket, std::string text)
{
    static_cast<void>(co_await socket->write(bytesOf(text)));
}

/// Reads from @p socket until EOF or an error: what a peer received.
/// @param socket The reader; must outlive the returned task.
/// @return Every byte read, in order.
inline async::Task<std::string> readUntilClosed(net::ISocket* socket)
{
    auto text = std::string {};
    auto chunk = std::vector<std::byte>(256);
    while (true)
    {
        auto const got = co_await socket->read(std::span<std::byte> { chunk });
        if (!got.has_value() || *got == 0)
            co_return text;
        text += textOf(std::span<std::byte const> { chunk }.first(*got));
    }
}

/// A body read to its end, chunk by chunk.
struct ReadBody
{
    std::string text;                    ///< Every body byte, in order.
    std::vector<std::size_t> chunkSizes; ///< The size of each chunk @c read handed out.
    std::optional<HttpError> error;      ///< What ended the read early, if anything did.
};

/// Reads @p reader until @c BodyEnd::Reached or an error, @p maxBytes at a time.
/// @param reader The body; must outlive the returned task.
/// @param maxBytes What each @c read asks for.
/// @return What was read and what, if anything, ended it.
inline async::Task<ReadBody> readAll(IBodyReader* reader, std::size_t maxBytes)
{
    auto out = ReadBody {};
    while (true)
    {
        auto chunk = co_await reader->read(maxBytes);
        if (!chunk.has_value())
        {
            out.error = std::move(chunk.error());
            co_return out;
        }
        out.chunkSizes.push_back(chunk->bytes.size());
        out.text += textOf(chunk->bytes);
        if (chunk->end == BodyEnd::Reached)
            co_return out;
    }
}

/// Runs @p task and stores its answer in @p out, for a case that drives the loop by hand and then
/// asserts the answer is there: a flow that never finishes leaves @p out empty, never a hang.
/// @tparam T The task's result.
/// @param task The flow.
/// @param out Where its answer goes; must outlive the returned task.
template <typename T>
async::Task<void> into(async::Task<T> task, std::optional<T>* out)
{
    *out = co_await std::move(task);
}

} // namespace core::http::test
