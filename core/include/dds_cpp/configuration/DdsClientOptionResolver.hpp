#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dds_cpp::detail {

/// 명시 경로 우선 적용과 실행 위치 기반 기본 파일 탐색을 담당한다. 환경변수 해석은 CLI에 둔다.
class DdsClientOptionResolver final {
public:
static std::string RequireFile(const std::filesystem::path& path, std::string_view option_name);
static std::optional<std::filesystem::path> FindFile(const std::vector<std::filesystem::path>& relative_paths);
static std::string ResolveRequiredFile(
    const std::string& configured_path,
    const std::filesystem::path& default_relative_path,
    std::string_view option_name);
};

} // namespace dds_cpp::detail
