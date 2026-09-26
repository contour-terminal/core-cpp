// SPDX-License-Identifier: Apache-2.0
#include <core/platform/PathUtils.hpp>
#include <core/platform/WorkingDirectory.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include <unistd.h>

namespace core::platform
{

namespace
{
    /// The process's working directory, through chdir(2) and getcwd(3).
    class PosixWorkingDirectory final: public WorkingDirectory
    {
      public:
        [[nodiscard]] std::expected<void, PlatformError> changeDirectory(
            std::filesystem::path const& path) override
        {
            if (::chdir(path.c_str()) != 0)
                return std::unexpected(PlatformError::FileNotFound);
            return {};
        }

        [[nodiscard]] std::string currentDirectory() const override
        {
            return normalizePath(std::filesystem::current_path());
        }
    };
} // namespace

std::unique_ptr<WorkingDirectory> nativeWorkingDirectory()
{
    return std::make_unique<PosixWorkingDirectory>();
}

} // namespace core::platform
