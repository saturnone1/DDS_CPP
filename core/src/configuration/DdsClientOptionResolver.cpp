#include "DdsClientOptionResolver.hpp"
#include "DdsUnicode.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace dds_cpp::detail {
namespace {

std::optional<std::filesystem::path> GetExecutableDirectory()
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

std::vector<std::filesystem::path> EnumerateBaseDirectories()
{
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    const auto append_with_parents = [&](std::filesystem::path directory) {
        for (int depth = 0; depth < 16; ++depth) {
            directory = std::filesystem::absolute(directory).lexically_normal();
            if (seen.insert(PathToUtf8(directory)).second) result.push_back(directory);
            if (!directory.has_parent_path() || directory.parent_path() == directory) break;
            directory = directory.parent_path();
        }
    };
    // .NET과 동일하게 현재 폴더를 먼저 찾고, 실행 파일 폴더와 그 상위 경로를 검색한다.
    const auto current = std::filesystem::absolute(std::filesystem::current_path()).lexically_normal();
    seen.insert(PathToUtf8(current));
    result.push_back(current);
    if (const auto directory = GetExecutableDirectory()) append_with_parents(*directory);
    return result;
}

} // namespace

std::string DdsClientOptionResolver::RequireFile(const std::filesystem::path& path, std::string_view option_name)
{
    if (path.empty()) throw DdsConfigurationException(std::string(option_name) + "이(가) 필요합니다.");
    const auto full_path = std::filesystem::absolute(path).lexically_normal();
    if (!std::filesystem::is_regular_file(full_path)) {
        throw DdsConfigurationException(std::string(option_name) + " 파일이 없습니다: " + PathToUtf8(full_path));
    }
    return PathToUtf8(full_path);
}

std::optional<std::filesystem::path> DdsClientOptionResolver::FindFile(const std::vector<std::filesystem::path>& relative_paths)
{
    for (const auto& directory : EnumerateBaseDirectories()) {
        for (const auto& relative_path : relative_paths) {
            const auto candidate = directory / relative_path;
            if (std::filesystem::is_regular_file(candidate)) {
                return std::filesystem::absolute(candidate).lexically_normal();
            }
        }
    }
    return std::nullopt;
}

std::string DdsClientOptionResolver::ResolveRequiredFile(
    const std::string& configured_path,
    const std::filesystem::path& default_relative_path,
    std::string_view option_name)
{
    if (configured_path.find_first_not_of(" \t\r\n") != std::string::npos)
        return RequireFile(std::filesystem::u8path(configured_path), option_name);
    if (const auto path = FindFile({
            default_relative_path,
            std::filesystem::path("share/dds_cpp") / default_relative_path})) {
        return PathToUtf8(*path);
    }
    throw DdsConfigurationException(
        std::string(option_name) + "이(가) 필요합니다. 경로를 지정하거나 " + PathToUtf8(default_relative_path) +
        " 파일을 실행 파일 옆 또는 현재 폴더에 두세요.");
}

} // namespace dds_cpp::detail
