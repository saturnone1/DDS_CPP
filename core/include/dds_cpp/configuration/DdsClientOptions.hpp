#pragma once

#include <cstdint>
#include "dds_cpp/api/DdsMessageModule.hpp"
#include <optional>
#include <string>
#include <vector>

#include "dds_cpp/api/DdsLogLevel.hpp"
#include "dds_cpp/api/TopicDefinition.hpp"
#include "dds_cpp/api/Visibility.hpp"

namespace dds_cpp {

/// 연결 하나의 모듈·토픽·도메인·QoS 설정이다. Connect는 이 설정을 연결 내부에 보관한다.
/// 비const 접근자는 연결 전에 값을 지정할 때 사용하며, 원본 옵션을 나중에 수정해도 기존 연결은 바뀌지 않는다.
struct DdsClientOptions final {
private:
    // 미지정은 선택된 모듈의 모든 메시지를 기본 QoS와 양방향으로 등록한다. 빈 목록은 메시지를 비활성화하며 설정은 생성 시 복사된다.
    std::optional<std::vector<TopicDefinition>> topics;
    std::string qos_profiles_xml_path{};
    std::string participant_name{};
    std::int32_t domain_id{0};
    std::vector<std::shared_ptr<const DdsMessageModule>> modules;
    std::string participant_qos{detail::default_qualified_qos_profile};
    std::string multicast_address;
    std::chrono::milliseconds shutdown_timeout{10000};
    DdsLogLevel log_level{DdsLogLevel::None};

public:
    /// 빌드된 메시지 모듈을 명시적으로 선택한다. Core에는 기본 메시지 XML이 내장되지 않는다.
    const auto& MessageModules() const noexcept { return modules; }
    auto& MessageModules() noexcept { return modules; }
    /// Participant의 QoS 프로파일이다. 독립 망에는 Library::Profile 형식으로 지정한다.
    const auto& ParticipantQosProfile() const noexcept { return participant_qos; }
    auto& ParticipantQosProfile() noexcept { return participant_qos; }
    /// Reader의 멀티캐스트 수신 주소 덮어쓰기다. 빈 문자열이면 QoS의 설정을 유지한다.
    const auto& MulticastReceiveAddress() const noexcept { return multicast_address; }
    auto& MulticastReceiveAddress() noexcept { return multicast_address; }
    /// 종료 호출자의 최대 대기 시간이다. 시간이 지나도 수신 중인 Reader를 강제로 파괴하지 않는다.
    auto ShutdownTimeout() const noexcept { return shutdown_timeout; }
    auto& ShutdownTimeout() noexcept { return shutdown_timeout; }
    /// DDS 도메인 번호다. 통신에는 도메인뿐 아니라 토픽·타입·QoS의 호환성도 필요하다.
    [[nodiscard]] std::int32_t DomainId() const noexcept { return domain_id; }
    std::int32_t& DomainId() noexcept { return domain_id; }
    /// nullopt는 선택 모듈의 전체 타입 등록, 빈 vector는 토픽 미등록을 뜻한다. 등록만으로 Reader를 생성하지 않는다.
    [[nodiscard]] const std::optional<std::vector<TopicDefinition>>& Topics() const noexcept { return topics; }
    std::optional<std::vector<TopicDefinition>>& Topics() noexcept { return topics; }
    /// QoS XML 경로다. 빈 값은 기본 경로 탐색을 사용한다. 망이 여러 개면 각각의 파일을 명시한다.
    [[nodiscard]] const std::string& QosProfilesXmlPath() const noexcept { return qos_profiles_xml_path; }
    std::string& QosProfilesXmlPath() noexcept { return qos_profiles_xml_path; }
    /// 탐색·로그에 사용할 Participant 이름이다. 이름만 다르게 해서는 망이 격리되지 않는다.
    [[nodiscard]] const std::string& ParticipantName() const noexcept { return participant_name; }
    std::string& ParticipantName() noexcept { return participant_name; }
    /// 진단 로그 수준이다. 오류 관찰자 등록과는 독립적이다.
    [[nodiscard]] DdsLogLevel LogLevel() const noexcept { return log_level; }
    DdsLogLevel& LogLevel() noexcept { return log_level; }
};

} // namespace dds_cpp
