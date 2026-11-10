// SPDX-License-Identifier: Apache-2.0
#include <core/Assert.hpp>
#include <core/detail/SystemRandom.hpp>

#include <sys/random.h>

#include <cerrno>

namespace core::detail
{

void fillFromSystemRandom(std::span<std::byte> out)
{
    while (!out.empty())
    {
        auto const got = ::getrandom(out.data(), out.size(), 0);
        if (got < 0)
        {
            if (errno == EINTR)
                continue;
            core::detail::fail("getrandom()", "The system random source failed.", __FILE__, __LINE__);
        }
        // A short read is allowed (a large request, or a signal part way): ask for the rest.
        out = out.subspan(static_cast<std::size_t>(got));
    }
}

} // namespace core::detail
