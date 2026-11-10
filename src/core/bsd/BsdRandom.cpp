// SPDX-License-Identifier: Apache-2.0
#include <core/detail/SystemRandom.hpp>

#include <cstdlib>

namespace core::detail
{

void fillFromSystemRandom(std::span<std::byte> out)
{
    // arc4random_buf() cannot fail, nor return short: the kernel seeds it, and it blocks until it can.
    ::arc4random_buf(out.data(), out.size());
}

} // namespace core::detail
