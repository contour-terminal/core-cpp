// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/platform/EnvironmentProvider.hpp>

#include <map>
#include <string>

namespace core::platform
{

/// @brief POSIX implementation of EnvironmentProvider using real OS calls.
///
/// Keeps variables set but not yet exported in memory. It reads the process environment through
/// @c core::LiveEnvironment, exports to it through @c core::setProcessEnvironmentVariable() and
/// @c core::unsetProcessEnvironmentVariable() (never `setenv()`, see there), and uses
/// chdir/getcwd for working directory operations.
class PosixEnvironmentProvider final: public EnvironmentProvider
{
  public:
    /// Returns the singleton instance.
    [[nodiscard]] static PosixEnvironmentProvider& instance();

    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;
    [[nodiscard]] std::vector<std::string> keys() const override;

    [[nodiscard]] std::expected<void, PlatformError> changeDirectory(
        std::filesystem::path const& path) override;
    [[nodiscard]] std::string currentDirectory() const override;

  private:
    std::map<std::string, std::string> _values;
};

} // namespace core::platform
