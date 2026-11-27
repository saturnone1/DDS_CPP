#pragma once
#include "dds_cpp/api/DdsMessageModule.hpp"
#include "dds_cpp/transport/RtiWriter.hpp"
#include "dds_cpp/transport/SubscriptionDispatcher.hpp"

namespace dds_cpp {
/// 생성 타입 T의 RTI 생성 함수를 등록 정보로 변환한다. 실제 네이티브 엔터티는 연결이 사용할 때 생성된다.
/// 이 템플릿은 메시지 모듈에서 구체화되므로 Core를 다시 빌드하지 않고 별도 모듈을 링크할 수 있다.
template<class T> MessageType RtiMessageType(std::string name)
{
    return {std::move(name), typeid(T),
        [](const auto& participant, const auto& publisher, auto& qos, const auto& topic, auto& mutex) {
            return std::make_unique<detail::RtiWriter<T>>(participant, publisher, qos, topic, mutex);
        },
        [](std::shared_ptr<void> owner, const auto& participant, const auto& subscriber, const auto& qos,
            const auto& topic, std::function<void(const void*)> handler, DdsLogLevel log, auto& mutex,
            std::chrono::milliseconds timeout, std::shared_ptr<detail::ClientStatus> status, std::uint64_t subscription_id) {
            auto subscription = std::make_unique<detail::SubscriptionHandle<T>>(
                std::move(owner), participant, subscriber, qos, topic, std::move(handler), log, mutex, timeout, std::move(status), subscription_id);
            auto state = subscription->state();
            return std::make_pair(std::unique_ptr<Subscription>(std::move(subscription)), state);
        }};
}
}
