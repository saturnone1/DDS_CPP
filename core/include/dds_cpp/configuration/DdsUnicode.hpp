#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace dds_cpp::detail {

inline std::string PathToUtf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

#if defined(_WIN32)
inline std::string to_utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length == 0) throw std::runtime_error("유니코드 문자열을 UTF-8로 변환할 수 없습니다.");
    std::string result(length, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) != length)
        throw std::runtime_error("유니코드 문자열을 UTF-8로 변환할 수 없습니다.");
    return result;
}
#endif

inline std::optional<std::string> ReadUtf8Environment(std::string_view name)
{
#if defined(_WIN32)
    const auto key = std::filesystem::u8path(name).wstring();
    auto size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    while (size > 0) {
        std::wstring buffer(size, L'\0');
        const auto length = GetEnvironmentVariableW(key.c_str(), buffer.data(), size);
        if (length == 0) return std::nullopt;
        if (length >= size) { size = length; continue; }
        return to_utf8(std::wstring_view(buffer.data(), length));
    }
    return std::nullopt;
#else
    const auto* value = std::getenv(std::string(name).c_str());
    return value && *value ? std::make_optional(std::string(value)) : std::nullopt;
#endif
}

// RTI 7.3.1의 Windows 파일 URL은 UTF-8이 아니라 시스템 코드 페이지를 사용합니다.
// 변환 불가능한 경로는 짧은 파일 이름을 시도하고, 손실 변환은 허용하지 않습니다.
inline std::string RtiFilePath(const std::string& utf8_path)
{
#if defined(_WIN32)
    auto wide = std::filesystem::u8path(utf8_path).wstring();
    const auto convert = [](const std::wstring& value) -> std::optional<std::string> {
        const auto page = GetACP();
        BOOL substituted = FALSE;
        auto* substitution = page == CP_UTF8 ? nullptr : &substituted;
        const auto flags = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        const auto length = WideCharToMultiByte(page, flags, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, substitution);
        if (length == 0 || substituted) return std::nullopt;
        std::string result(length, '\0');
        if (WideCharToMultiByte(page, flags, value.data(), static_cast<int>(value.size()),
                result.data(), length, nullptr, substitution) != length || substituted)
            return std::nullopt;
        return result;
    };
    if (const auto result = convert(wide)) return *result;
    const auto size = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (size > 0) {
        std::wstring short_path(size, L'\0');
        const auto length = GetShortPathNameW(wide.c_str(), short_path.data(), size);
        if (length > 0 && length < size) {
            short_path.resize(length);
            if (const auto result = convert(short_path)) return *result;
        }
    }
    throw std::runtime_error("RTI가 이 파일 경로의 문자를 처리할 수 없습니다. 다른 폴더에 배치하세요: " + utf8_path);
#else
    return utf8_path;
#endif
}

} // namespace dds_cpp::detail
