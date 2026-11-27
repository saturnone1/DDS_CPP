#pragma once
#include "dds_cpp/api/TopicDefinition.hpp"
namespace dds_cpp {
/// 생성 타입으로 토픽 등록 정보를 만드는 도우미다. 반환값을 Options.Topics에 넣어야 연결 설정에 반영된다.
/// 이 함수들은 네이티브 Topic·Writer·Reader를 생성하지 않는다.
struct Topic final {
    /// 타입의 기본 이름과 다른 토픽 이름을 지정한다. 같은 타입을 여러 토픽에서 쓸 때도 사용한다.
    template<class T> static TopicDefinition Named(std::string name, TopicDirection direction = TopicDirection::Both,
        std::optional<std::string> qos = std::nullopt) { return {std::move(name), std::move(qos), direction, typeid(T)}; }
    /// 발행 전용 등록이다. 빈 토픽 이름은 설정 검증기가 모듈에 등록된 타입 이름으로 확정한다.
    template<class T> static TopicDefinition Publish(std::optional<std::string> qos = std::nullopt)
    { return Named<T>("", TopicDirection::Publish, std::move(qos)); }
    /// 수신 전용 등록이다. 실제 구독은 DdsClient::Subscribe를 호출해야 시작된다.
    template<class T> static TopicDefinition Subscribe(std::optional<std::string> qos = std::nullopt)
    { return Named<T>("", TopicDirection::Subscribe, std::move(qos)); }
    /// 이 타입의 기본 토픽에서 발행과 구독을 모두 허용한다.
    template<class T> static TopicDefinition Both(std::optional<std::string> qos = std::nullopt)
    { return Named<T>("", TopicDirection::Both, std::move(qos)); }
};
}
