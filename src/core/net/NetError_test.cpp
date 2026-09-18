// SPDX-License-Identifier: Apache-2.0
#include <core/net/IoResult.hpp>
#include <core/net/NetError.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <set>
#include <string_view>
#include <type_traits>

using core::net::IoResult;
using core::net::makeNetError;
using core::net::NetError;
using core::net::NetErrorCode;

// The zero enumerator is the absent case, and a code is one byte: what cpp-guidelines.md asks of an
// enum class, checked where the vocabulary is defined.
static_assert(std::is_same_v<std::underlying_type_t<NetErrorCode>, std::uint8_t>);
static_assert(static_cast<std::uint8_t>(NetErrorCode::Ok) == 0);
static_assert(NetErrorCode {} == NetErrorCode::Ok);

// toString is usable in a constant expression, so a table of descriptions can be built at compile time.
static_assert(core::net::toString(NetErrorCode::Eof) == "end of stream");

// The result of a byte transfer is a count or an error, and nothing else.
static_assert(std::is_same_v<IoResult, std::expected<std::size_t, NetError>>);

namespace
{

/// Every enumerator, so a new one without a description fails the distinctness case below.
constexpr auto AllCodes = std::array {
    NetErrorCode::Ok,           NetErrorCode::Eof,         NetErrorCode::Cancelled,
    NetErrorCode::Timeout,      NetErrorCode::WouldBlock,  NetErrorCode::BadHandle,
    NetErrorCode::ConnReset,    NetErrorCode::ConnRefused, NetErrorCode::AddressInUse,
    NetErrorCode::AddressError, NetErrorCode::Unsupported, NetErrorCode::MessageTooLarge,
    NetErrorCode::Other,
};

} // namespace

TEST_CASE("Every NetErrorCode has a description of its own", "[net][types]")
{
    auto seen = std::set<std::string_view> {};
    for (auto const code: AllCodes)
    {
        auto const text = core::net::toString(code);
        CHECK_FALSE(text.empty());
        CHECK(text != "unknown error");
        CHECK(seen.insert(text).second);
    }
    CHECK(seen.size() == AllCodes.size());
}

TEST_CASE("A value outside the enumeration is described as unknown", "[net][types]")
{
    CHECK(core::net::toString(static_cast<NetErrorCode>(0xFF)) == "unknown error");
}

TEST_CASE("NetError describes its category, then its context, then the OS code", "[net][types]")
{
    CHECK(makeNetError(NetErrorCode::ConnReset, 104, "recv").toString()
          == "connection reset (recv) [errno 104]");
    CHECK(makeNetError(NetErrorCode::Eof).toString() == "end of stream");
    CHECK(makeNetError(NetErrorCode::Timeout, 0, "connect").toString() == "timed out (connect)");
    CHECK(makeNetError(NetErrorCode::Other, 13).toString() == "network error [errno 13]");
}

TEST_CASE("makeNetError fills every field, and a default NetError is an unclassified one", "[net][types]")
{
    auto const error = makeNetError(NetErrorCode::AddressInUse, 98, "bind");
    CHECK(error.code == NetErrorCode::AddressInUse);
    CHECK(error.systemCode == 98);
    CHECK(error.context == "bind");

    auto const unclassified = NetError {};
    CHECK(unclassified.code == NetErrorCode::Other);
    CHECK(unclassified.systemCode == 0);
    CHECK(unclassified.context.empty());
}

TEST_CASE("IoResult carries the transferred count or the error", "[net][types]")
{
    auto const transferred = IoResult { std::size_t { 42 } };
    REQUIRE(transferred.has_value());
    CHECK(*transferred == 42);

    auto const failed = IoResult { std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "read")) };
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().code == NetErrorCode::BadHandle);
    CHECK(failed.error().context == "read");
}
