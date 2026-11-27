#pragma once
#include "dds_cpp/transport/RtiHeaders.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <vector>
#include "dds_cpp/api/TopicDefinition.hpp"
#include "dds_cpp/api/Subscription.hpp"
#include "dds_cpp/api/DdsLogLevel.hpp"
#include "dds_cpp/transport/ClientStatus.hpp"

namespace dds_cpp {
namespace detail { class IRtiWriter; class ISubscriptionDispatcher; }
/// 생성 타입과 RTI Writer·Reader 생성 함수를 묶는 타입 소거 등록 정보다.
/// Core는 개별 메시지 헤더를 참조하지 않고 선택된 모듈의 생성 함수를 통해 네이티브 엔터티를 만든다.
struct MessageType {
    std::string name;
    std::type_index type{typeid(void)};
    std::function<std::unique_ptr<detail::IRtiWriter>(
        const dds::domain::DomainParticipant&, const dds::pub::Publisher&,
        dds::core::QosProvider&, const TopicDefinition&, std::mutex&)> create_writer;
    std::function<std::pair<std::unique_ptr<Subscription>, std::weak_ptr<detail::ISubscriptionDispatcher>>(
        std::shared_ptr<void>, const dds::domain::DomainParticipant&, const dds::sub::Subscriber&,
        const dds::sub::qos::DataReaderQos&, const TopicDefinition&, std::function<void(const void*)>,
        DdsLogLevel, std::mutex&, std::chrono::milliseconds, std::shared_ptr<detail::ClientStatus>, std::uint64_t)> create_reader;
};
/// XML 하나에서 생성한 타입 목록이다. 서로 다른 XML의 동일한 타입 이름은 생성 네임스페이스로 구분한다.
/// 같은 연결에 넣을 때의 중복 여부는 연결 설정 검증기가 확인한다.
struct DdsMessageModule {
    std::string name;
    std::string version;
    std::vector<MessageType> types;
    // 생성 XML과의 일치 여부를 확인할 수 있는 메타데이터다. DDS 통신 타입 이름이나 전송 데이터에는 포함되지 않는다.
    std::string schema_sha256;
    // C# 속성과 같은 이름의 읽기 전용 조회 API다. 기존 값 필드는 소스 호환성을 위해 유지한다.
    const std::string& Name() const noexcept { return name; }
    const std::string& Version() const noexcept { return version; }
    const std::vector<MessageType>& Types() const noexcept { return types; }
    const std::string& SchemaSha256() const noexcept { return schema_sha256; }
};
}
