#pragma once

#include <string>
#include <optional>
#include <typeindex>

#include "dds_cpp/api/TopicDirection.hpp"
#include "dds_cpp/api/Visibility.hpp"
#include "dds_cpp/configuration/DdsQosDefaults.hpp"

namespace dds_cpp {

// 헤더 전용 값 타입이다. 정적 메시지 모듈 사이에서 인라인 함수를 DLL 심볼로 내보내지 않는다.
struct TopicDefinition final {
    std::string name;
    std::optional<std::string> qos_profile;
    TopicDirection direction{TopicDirection::Both};
    std::type_index sample_type{typeid(void)};

    [[nodiscard]] std::string QualifiedQosProfile() const
    {
        const auto& profile = QosProfile();
        return profile.find("::") != std::string::npos ? profile : std::string(detail::default_qos_library) + "::" + profile;
    }

    [[nodiscard]] const std::string& Name() const noexcept { return name; }
    [[nodiscard]] const std::string& QosProfile() const noexcept { static const std::string fallback = detail::default_topic_profile; return qos_profile ? *qos_profile : fallback; }
    [[nodiscard]] TopicDirection Direction() const noexcept { return direction; }
    [[nodiscard]] std::type_index SampleType() const noexcept { return sample_type; }
};

} // namespace dds_cpp
