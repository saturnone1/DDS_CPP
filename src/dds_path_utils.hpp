#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dds_cpp::detail {

std::optional<std::string> environment(std::string_view name);
std::string require_file(const std::filesystem::path& path, std::string_view option_name);
std::optional<std::filesystem::path> find_file(const std::vector<std::filesystem::path>& relative_paths);
std::string resolve_required_file(
    const std::string& configured_path,
    const std::filesystem::path& default_relative_path,
    std::string_view option_name);
std::optional<std::filesystem::path> resolve_config_file(const std::optional<std::string>& requested_path);

} // namespace dds_cpp::detail
