#pragma once
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <typeinfo>
#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "dds_cpp/configuration/DdsConfiguration.hpp"
#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"
#include "dds_cpp/api/Subscription.hpp"
#include "dds_cpp/api/Topic.hpp"
#include "dds_cpp/transport/ClientStatus.hpp"

#include "dds_cpp/api/DdsPublisher.hpp"
#include "dds_cpp/api/DdsSampleInfo.hpp"
namespace dds_cpp {
class IDdsTransport;
namespace detail { class ClientAccess; }
/// 하나의 Participant와 그 망에 속한 발행·구독 자원의 소유자다.
/// 복사는 허용하지 않고 이동만 허용한다. 다른 망은 별도의 DdsClient 인스턴스로 연결한다.
class DDSCPP_API DdsClient final {
public:
    /// 로컬 자원을 생성하고 설정을 검증한다. 반환 시점에 원격 가입자가 모두 발견되었다는 뜻은 아니다.
    static DdsClient Connect(DdsClientOptions options = {});
    DdsClient(const DdsClient&) = delete;
    DdsClient& operator=(const DdsClient&) = delete;
    DdsClient(DdsClient&& other) noexcept;
    DdsClient& operator=(DdsClient&& other) noexcept;
    ~DdsClient();
    /// 신규 작업을 차단하고 정리를 요청한다. 제한 시간 뒤에도 사용 중인 네이티브 자원을 강제 삭제하지 않는다.
    /// 실패 정보는 ShutdownError로 조회한다. 소멸자에서도 호출되므로 예외를 외부로 던지지 않는다.
    void Close() noexcept;
    /// 종료 요청과 실제 자원 해제의 완료·실패를 구분한 상태다.
    [[nodiscard]] DdsShutdownState ShutdownState() const;
    /// 종료 중 보존한 예외다. 예외가 없으면 비어 있는 exception_ptr를 반환한다.
    [[nodiscard]] std::exception_ptr ShutdownError() const;
    /// 정리 완료를 제한 시간 동안 기다린다. 자신의 수신 콜백에서는 자기 대기를 피한다.
    bool WaitForShutdown(std::chrono::milliseconds timeout) const;
    /// 가장 최근 수신 오류다. 전체 이력을 보관하려면 별도의 오류 핸들러를 등록한다.
    [[nodiscard]] std::optional<DdsSubscriptionError> LastSubscriptionError() const;
    // 수신 스레드에서 호출된다. 오래 기다리지 않아야 하며 기존 오류 관찰자를 교체한다.
    void SetSubscriptionErrorHandler(std::function<void(const DdsSubscriptionError&)> handler);
    /// 기존 관찰자를 유지하면서 추가한다. 반환 핸들을 파괴하면 이 관찰자만 제거한다.
    [[nodiscard]] std::unique_ptr<Subscription> ObserveSubscriptionErrors(std::function<void(const DdsSubscriptionError&)> handler);
    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] const DdsClientOptions& Options() const noexcept;
    [[nodiscard]] const DdsConfiguration& Configuration() const noexcept;

    /// Writer를 미리 생성한다. 토픽 이름을 생략하려면 이 타입에 연결된 토픽이 정확히 하나여야 한다.
    template<class T> [[nodiscard]] DdsPublisher<T> CreatePublisher(const std::string& topic_name = {}) {
        const auto& topic = ResolveTopic(typeid(T), topic_name);
        return DdsPublisher<T>(topic, options_.LogLevel(), CreatePublisher(topic, typeid(T)));
    }
    /// 한 번 발행한다. 최초 발행 전 탐색 완료를 기다리려면 CreatePublisher의 반환 핸들을 사용한다.
    template<class T> void Publish(const T& sample, const std::string& topic_name = {}) {
        const auto transport = RequireTransport();
        const auto& topic = ResolveTopic(typeid(T), topic_name, false);
        EnsureCanPublish(topic);
        detail::DdsClientLog::Sample("TX", options_.LogLevel(), topic, sample);
        PublishThroughTransport(transport, topic, typeid(T), &sample);
    }
    /// 전용 수신 스레드에서 핸들러를 호출하는 구독을 만든다. 반환 핸들을 파괴하면 해당 구독의 종료를 요청한다.
    /// 샘플 참조는 콜백 동안만 유효하다. 나중에 사용하려면 T 값으로 복사하고, UI 갱신은 UI 스레드로 전달한다.
    template<class T> [[nodiscard]] std::unique_ptr<Subscription> Subscribe(std::function<void(const T&)> handler, const std::string& topic_name = {}) {
        return SubscribeCore<T>(std::move(handler), topic_name, {});
    }
    /// C++ 구독은 원래 대여 참조를 전달한다. C#의 명시적 대여 API와 같은 이름으로도 사용할 수 있다.
    template<class T> [[nodiscard]] std::unique_ptr<Subscription> SubscribeBorrowed(std::function<void(const T&)> handler,
        const std::string& topic_name = {}) { return Subscribe<T>(std::move(handler), topic_name); }
    template<class T> [[nodiscard]] std::unique_ptr<Subscription> SubscribeBorrowed(std::function<void(const T&)> handler,
        const std::string& topic_name, std::function<void(const DdsSubscriptionError&)> on_error)
        { return Subscribe<T>(std::move(handler), topic_name, std::move(on_error)); }
    /// 수신 시작 전에 이 구독의 오류 관찰자를 등록한다. 관찰자의 예외는 다른 오류 통지를 중단하지 않는다.
    template<class T> [[nodiscard]] std::unique_ptr<Subscription> Subscribe(std::function<void(const T&)> handler,
        const std::string& topic_name, std::function<void(const DdsSubscriptionError&)> on_error) {
        if (!on_error) throw std::invalid_argument("구독별 오류 관찰자가 필요합니다.");
        return SubscribeCore<T>(std::move(handler), topic_name, std::move(on_error));
    }
    /// C#과 같은 대여 샘플·시각 정보 구독이다. 기존 오류 통지와 종료 경로를 그대로 사용한다.
    template<class T> [[nodiscard]] std::unique_ptr<Subscription> SubscribeWithInfoBorrowed(
        std::function<void(const T&, const DdsSampleInfo&)> handler, const std::string& topic_name = {},
        std::function<void(const DdsSubscriptionError&)> on_error = {}) {
        if (!handler) throw std::invalid_argument("DDS 구독에는 핸들러가 필요합니다.");
        return SubscribeCore<T>([handler = std::move(handler)](const T& sample) {
            const auto* native_info = detail::ActiveSampleInfo();
            const DdsSampleInfo info = native_info ? *native_info : DdsSampleInfo{std::chrono::system_clock::now(), std::nullopt};
            handler(sample, info);
        }, topic_name, std::move(on_error));
    }
private:
    template<class T> std::unique_ptr<Subscription> SubscribeCore(std::function<void(const T&)> handler,
        const std::string& topic_name, std::function<void(const DdsSubscriptionError&)> on_error) {
        if (!handler) throw std::invalid_argument("DDS 구독에는 핸들러가 필요합니다.");
        const auto topic = ResolveTopic(typeid(T), topic_name);
        return CreateSubscriber(topic, typeid(T), [log_level = options_.LogLevel(), topic, handler = std::move(handler)](const void* sample) {
            const auto& typed = *static_cast<const T*>(sample);
            detail::DdsClientLog::Sample("RX", log_level, topic, typed);
            handler(typed);
        }, std::move(on_error));
    }
private:
    friend class detail::ClientAccess;
    explicit DdsClient(DdsClientOptions options);
    DdsClient(DdsClientOptions options, DdsConfiguration configuration);
    DdsClient(DdsClientOptions options, std::unique_ptr<IDdsTransport> transport);
    DdsClient(DdsClientOptions options, DdsConfiguration configuration, std::unique_ptr<IDdsTransport> transport);
    static void EnsureCanPublish(const TopicDefinition& topic);
    static void EnsureCanSubscribe(const TopicDefinition& topic);
    [[nodiscard]] std::shared_ptr<IDdsTransport> RequireTransport() const;
    [[nodiscard]] const TopicDefinition& ResolveTopic(const std::type_info& type, const std::string& name = {}, bool check_open = true) const;
    static void PublishThroughTransport(const std::shared_ptr<IDdsTransport>& transport, const TopicDefinition& topic,
        const std::type_info& type, const void* sample);
    std::shared_ptr<IDdsTransport> CreatePublisher(const TopicDefinition& topic, const std::type_info& type);
    std::unique_ptr<Subscription> CreateSubscriber(const TopicDefinition& topic, const std::type_info& type,
        std::function<void(const void*)> handler, std::function<void(const DdsSubscriptionError&)> on_error = {});
    DdsClientOptions options_;
    DdsConfiguration configuration_;
    mutable std::mutex transport_mutex_;
    std::shared_ptr<IDdsTransport> transport_;
    std::shared_ptr<detail::ClientStatus> status_;
};
}
