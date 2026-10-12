#pragma once
#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"
#include "dds_cpp/diagnostics/DdsClientLog.hpp"
#include <memory>
#include <functional>
#include <typeinfo>
namespace dds_cpp {
class IDdsTransport;
namespace detail {
DDSCPP_API void PublishSample(const std::weak_ptr<IDdsTransport>& transport,
    const TopicDefinition& topic, const std::type_info& type, const void* sample);
DDSCPP_API std::function<void(const void*)> BindPublisher(const std::shared_ptr<IDdsTransport>& transport,
    const TopicDefinition& topic, const std::type_info& type);
DDSCPP_API void WaitForSubscribers(const std::weak_ptr<IDdsTransport>& transport,
    const TopicDefinition& topic, const std::type_info& type, int count, std::chrono::milliseconds timeout);
DDSCPP_API void WaitForAcknowledgments(const std::weak_ptr<IDdsTransport>& transport,
    const TopicDefinition& topic, const std::type_info& type, std::chrono::milliseconds timeout);
}
/// 하나의 생성 타입과 토픽에 바인딩된 발행 핸들이다.
/// 전송 계층을 weak_ptr로 참조하므로 이 핸들만 보관해도 종료된 연결의 수명이 늘어나지는 않는다.
template<class T> class DdsPublisher final {
public:
    /// 생성 시 확정한 토픽 정보다. 참조의 수명은 이 발행 핸들의 수명과 같다.
    [[nodiscard]] const TopicDefinition& Topic() const noexcept { return topic_; }
    /// 호환되는 원격 Reader가 count개 이상 매칭될 때까지 기다린다. 앱의 메시지 처리 준비까지 확인하지는 않는다.
    void WaitForSubscribers(int count, std::chrono::milliseconds timeout) const {
        detail::WaitForSubscribers(transport_, topic_, typeid(T), count, timeout);
    }
    // DDS 수신 확인만 기다린다. 애플리케이션 핸들러의 처리 완료를 의미하지 않는다.
    void WaitForAcknowledgments(std::chrono::milliseconds timeout) const {
        detail::WaitForAcknowledgments(transport_, topic_, typeid(T), timeout);
    }
    /// 샘플을 DDS Writer에 전달한다. 신뢰성 QoS에 따라 지연될 수 있으며 상대 핸들러 완료를 기다리지 않는다.
    void Publish(const T& sample) const {
        detail::DdsClientLog::Sample("TX", log_level_, topic_, sample);
        write_(&sample);
    }
private:
    friend class DdsClient;
    DdsPublisher(TopicDefinition topic, DdsLogLevel log_level, std::shared_ptr<IDdsTransport> transport)
        : topic_(std::move(topic)), log_level_(log_level), transport_(transport),
          write_(detail::BindPublisher(transport, topic_, typeid(T))) {}
    TopicDefinition topic_;
    DdsLogLevel log_level_;
    std::weak_ptr<IDdsTransport> transport_;
    std::function<void(const void*)> write_;
};

}
