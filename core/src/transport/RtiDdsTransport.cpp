#include "RtiDdsTransport.hpp"

#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <sstream>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"
#include "dds_cpp/api/DdsTimeoutException.hpp"
#include "DdsClientOptionResolver.hpp"
#include "DdsUnicode.hpp"
#include "DdsConfigurationReader.hpp"

#if defined(DDSCPP_ENABLE_RTI)
#include "RtiReaderQosOverride.hpp"
#include "RtiTransportResources.hpp"
#include <dds/dds.hpp>
#include <rti/core/policy/CorePolicy.hpp>
#endif

namespace dds_cpp {

#if defined(DDSCPP_ENABLE_RTI)

namespace {

using namespace detail;

} // namespace

// 전송 객체와 각 수신 상태가 공유하는 네이티브 자원 묶음이다.
// 공개 연결 핸들이 닫혀도 진행 중인 콜백이 소유권을 유지하여 Participant 조기 파괴를 막는다.
class RtiDdsTransport::TransportState final : public std::enable_shared_from_this<TransportState> {
public:
    explicit TransportState(DdsClientOptions options, std::shared_ptr<ClientStatus> status)
        : options_(std::move(options)), resources_(options_, std::move(status))
    {
    }

    void Dispose() noexcept
    {
        std::vector<std::weak_ptr<ISubscriptionDispatcher>> readers;
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            readers.swap(readers_);
        }
        // 상태 잠금을 풀고 종료를 요청한다. 사용자 핸들러의 종료를 기다리는 동안 등록 잠금을 잡지 않는다.
        StopSubscriptions(readers, options_.ShutdownTimeout());
    }

    void EnsureOpen() const {
        if (closed_) throw DdsOperationException("DDS 클라이언트가 닫혔습니다.");
    }

    void CreatePublisher(const TopicDefinition& topic, const std::type_info& type)
    {
        (void)GetOrCreateWriter(topic, type);
    }

    std::function<void(const void*)> BindPublisher(const TopicDefinition& topic, const std::type_info& type)
    {
        auto* writer = GetOrCreateWriter(topic, type);
        return [weak = weak_from_this(), writer](const void* sample) {
            // 강한 상태 참조를 작업 동안만 획득한다. 캐시 잠금 없이도 Writer와 부모 자원의 수명을 보호한다.
            const auto owner = weak.lock();
            if (!owner || owner->closed_.load(std::memory_order_acquire))
                throw DdsOperationException("DDS 클라이언트가 닫혔습니다.");
            if (!sample) throw DdsOperationException("RTI 발행에는 샘플이 필요합니다.");
            writer->Write(sample);
        };
    }

    // 캐시 조회·생성만 잠금 안에서 수행한다. 반환된 Writer의 사용 기간은 호출 경로의 공유 소유권으로 보호한다.
    IRtiWriter* GetOrCreateWriter(const TopicDefinition& topic, const std::type_info& type)
    {
        std::lock_guard lock(mutex_);
        EnsureOpen();
        const auto key = CreateTopicKey(topic, type);
        auto it = writers_.find(key);
        if (it == writers_.end())
            it = writers_.emplace(WriterKey{std::string(key.name), key.type}, CreateWriter(topic, type)).first;
        return it->second.get();
    }

    void WaitForSubscribers(const TopicDefinition& topic, const std::type_info& type,
        int count, std::chrono::milliseconds timeout)
    {
        auto* writer = GetOrCreateWriter(topic, type);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (writer->MatchedCount() < count) {
            { std::lock_guard lock(mutex_); EnsureOpen(); }
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= decltype(remaining)::zero())
                throw DdsTimeoutException("Topic '" + topic.name + "': 구독자 연결 대기 시간이 초과되었습니다: " + std::to_string(count) + " 명의 구독자.");
            std::this_thread::sleep_for((std::min)(remaining,
                std::chrono::steady_clock::duration(std::chrono::milliseconds(20))));
        }
    }

    void WaitForAcknowledgments(const TopicDefinition& topic, const std::type_info& type,
        std::chrono::milliseconds timeout)
    {
        GetOrCreateWriter(topic, type)->WaitForAcknowledgments(timeout);
    }

    void Publish(const TopicDefinition& topic, const std::type_info& type, const void* sample)
    {
        if (sample == nullptr) throw DdsOperationException("RTI 발행에는 샘플이 필요합니다.");
        // Write는 신뢰성 QoS의 역압력 때문에 지연될 수 있다. GetOrCreateWriter의 잠금이 해제된 다음 호출한다.
        GetOrCreateWriter(topic, type)->Write(sample);
    }

    std::unique_ptr<Subscription> Subscribe(
        const TopicDefinition& topic,
        const std::type_info& type,
        TypedSampleHandler handler, std::uint64_t subscription_id)
    {
        if (!handler) throw DdsOperationException("RTI 구독에는 핸들러가 필요합니다.");

        // 모듈의 타입 소거 생성 함수로 Reader를 만든다. shared_from_this는 수신 상태의 부모 자원 소유권이다.
        auto result = FindMessageType(type).create_reader(shared_from_this(), resources_.Participant, resources_.Subscriber,
            resources_.ReaderQosOverride.ReaderQos(resources_.Provider, topic.QualifiedQosProfile()), topic,
            std::move(handler), options_.LogLevel(), topic_mutex_, options_.ShutdownTimeout(), resources_.Status, subscription_id);
        {
            std::lock_guard lock(mutex_);
            EnsureOpen();
            // 반복 구독·해제로 끝난 Reader의 약한 참조가 무한히 쌓이지 않도록 등록 시 정리한다.
            std::erase_if(readers_, [](const auto& weak) { return weak.expired(); });
            readers_.push_back(result.second);
        }
        return std::move(result.first);
    }

private:
    const MessageType& FindMessageType(const std::type_info& type) const
    {
        for (const auto& module : options_.MessageModules())
            for (const auto& entry : module->types)
                if (entry.type == std::type_index(type)) return entry;
        throw DdsConfigurationException("이 연결에 등록되지 않은 메시지 타입입니다.");
    }

    std::unique_ptr<IRtiWriter> CreateWriter(const TopicDefinition& topic, const std::type_info& type)
    {
        return FindMessageType(type).create_writer(resources_.Participant, resources_.Publisher, resources_.Provider, topic, topic_mutex_);
    }

    DdsClientOptions options_;
    RtiTransportResources resources_;
    std::mutex mutex_;
    std::mutex topic_mutex_;
    std::atomic_bool closed_{false};
    std::vector<std::weak_ptr<ISubscriptionDispatcher>> readers_;
    std::unordered_map<WriterKey, std::unique_ptr<IRtiWriter>, WriterKeyHash, WriterKeyEqual> writers_;
};

#else

class RtiDdsTransport::TransportState final {
};

#endif

RtiDdsTransport::RtiDdsTransport(DdsClientOptions options)
{
#if defined(DDSCPP_ENABLE_RTI)
    try {
        state_ = std::make_shared<TransportState>(std::move(options), status_);
    } catch (const DdsConfigurationException&) {
        throw;
    } catch (const std::exception& ex) {
        throw DdsOperationException("RTI Connext DDS 초기화에 실패했습니다: " + std::string(ex.what()));
    }
#else
    (void)options;
    throw DdsOperationException("RtiDdsTransport가 RTI Connext DDS 지원 없이 빌드되었습니다.");
#endif
}

RtiDdsTransport::~RtiDdsTransport() = default;

std::function<void(const void*)> RtiDdsTransport::BindPublisher(const TopicDefinition& topic, const std::type_info& type)
{
#if defined(DDSCPP_ENABLE_RTI)
    return state_->BindPublisher(topic, type);
#else
    (void)topic; (void)type;
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

void RtiDdsTransport::CreatePublisher(const TopicDefinition& topic, const std::type_info& sample_type)
{
#if defined(DDSCPP_ENABLE_RTI)
    state_->CreatePublisher(topic, sample_type);
#else
    (void)topic;
    (void)sample_type;
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

std::unique_ptr<Subscription> RtiDdsTransport::Subscribe(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    TypedSampleHandler handler, std::uint64_t subscription_id)
{
#if defined(DDSCPP_ENABLE_RTI)
    return state_->Subscribe(topic, sample_type, std::move(handler), subscription_id);
#else
    (void)topic;
    (void)sample_type;
    (void)handler;
    (void)subscription_id;
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

void RtiDdsTransport::Publish(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    const void* sample)
{
#if defined(DDSCPP_ENABLE_RTI)
    state_->Publish(topic, sample_type, sample);
#else
    (void)topic;
    (void)sample_type;
    (void)sample;
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

void RtiDdsTransport::WaitForSubscribers(const TopicDefinition& topic, const std::type_info& type,
    int count, std::chrono::milliseconds timeout)
{
#if defined(DDSCPP_ENABLE_RTI)
    state_->WaitForSubscribers(topic, type, count, timeout);
#else
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

void RtiDdsTransport::WaitForAcknowledgments(const TopicDefinition& topic, const std::type_info& type,
    std::chrono::milliseconds timeout)
{
#if defined(DDSCPP_ENABLE_RTI)
    state_->WaitForAcknowledgments(topic, type, timeout);
#else
    throw DdsOperationException("RTI Connext DDS 지원이 비활성화되어 있습니다.");
#endif
}

void RtiDdsTransport::Dispose() noexcept
{
#if defined(DDSCPP_ENABLE_RTI)
    state_->Dispose();
#endif
}

} // namespace dds_cpp
