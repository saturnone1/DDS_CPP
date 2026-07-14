#include "dds_path_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "dds_cpp/dds_exception.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dds_cpp::detail {
namespace {

std::optional<std::filesystem::path> executable_directory()
{
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::nullopt : std::make_optional(executable.parent_path());
#else
    return std::nullopt;
#endif
}

std::vector<std::filesystem::path> search_directories()
{
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    const auto append_with_parents = [&](std::filesystem::path directory) {
        for (int depth = 0; depth < 16; ++depth) {
            directory = std::filesystem::absolute(directory).lexically_normal();
            if (seen.insert(directory.string()).second) result.push_back(directory);
            if (!directory.has_parent_path() || directory.parent_path() == directory) break;
            directory = directory.parent_path();
        }
    };
    append_with_parents(std::filesystem::current_path());
    if (const auto directory = executable_directory()) append_with_parents(*directory);
    return result;
}

} // namespace

std::optional<std::string> environment(std::string_view name)
{
    const auto* value = std::getenv(std::string(name).c_str());
    return value != nullptr && *value != '\0'
        ? std::make_optional(std::string(value))
        : std::nullopt;
}

std::string require_file(const std::filesystem::path& path, std::string_view option_name)
{
    if (path.empty()) throw DdsConfigurationException(std::string(option_name) + " is required.");
    const auto full_path = std::filesystem::absolute(path).lexically_normal();
    if (!std::filesystem::is_regular_file(full_path)) {
        throw DdsConfigurationException(std::string(option_name) + " does not exist: " + full_path.string());
    }
    return full_path.string();
}

std::optional<std::filesystem::path> find_file(const std::vector<std::filesystem::path>& relative_paths)
{
    for (const auto& directory : search_directories()) {
        for (const auto& relative_path : relative_paths) {
            const auto candidate = directory / relative_path;
            if (std::filesystem::is_regular_file(candidate)) {
                return std::filesystem::absolute(candidate).lexically_normal();
            }
        }
    }
    return std::nullopt;
}

std::string resolve_required_file(
    const std::string& configured_path,
    const std::filesystem::path& default_relative_path,
    std::string_view option_name)
{
    if (!configured_path.empty()) return require_file(configured_path, option_name);
    if (const auto path = find_file({
            default_relative_path,
            std::filesystem::path("DDS_Nuget") / default_relative_path,
            std::filesystem::path("share/dds_cpp") / default_relative_path})) {
        return path->string();
    }
    throw DdsConfigurationException(
        std::string(option_name) + " is required. Set it explicitly or place " + default_relative_path.string() +
        " beside the executable or under the current directory.");
}

std::optional<std::filesystem::path> resolve_config_file(const std::optional<std::string>& requested_path)
{
    const auto selected = requested_path.has_value() ? requested_path : environment("DDS_CLIENT_CONFIG_PATH");
    if (selected.has_value() && !selected->empty()) {
        return std::filesystem::path(require_file(*selected, "DDS client config"));
    }
    return find_file({
        "definitions/dds_client.xml",
        "DDS_Nuget/definitions/dds_client.xml",
        "share/dds_cpp/definitions/dds_client.xml"});
}

} // namespace dds_cpp::detail
