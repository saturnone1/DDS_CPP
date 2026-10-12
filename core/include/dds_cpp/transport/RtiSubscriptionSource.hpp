#pragma once
#include "dds_cpp/transport/RtiTopic.hpp"
#include "dds_cpp/api/DdsSampleInfo.hpp"
#include <atomic>
#include <optional>

namespace dds_cpp::detail {
/// Reader와 Topic을 소유하고 한 배치의 대여 샘플을 전달하는 네이티브 소스다.
/// 호출 순서는 CreateReader → Dispatch 반복 → Dispose다. 실행 일정과 종료 대기는 수신기가 담당한다.
template<class T> class RtiSubscriptionSource final {
public:
    RtiSubscriptionSource(const dds::domain::DomainParticipant& participant,
        const TopicDefinition& definition, std::mutex& topic_mutex)
        : topic_(GetOrCreateTopic<T>(participant, definition.name, topic_mutex)) {}

    void CreateReader(const dds::sub::Subscriber& subscriber, const dds::sub::qos::DataReaderQos& qos,
        dds::sub::NoOpDataReaderListener<T>* listener)
    {
        reader_.emplace(subscriber, topic_, qos, listener, dds::core::status::StatusMask::data_available());
    }

    template<class Deliver> void Dispatch(const std::atomic_bool& stopping, Deliver deliver)
    {
        // 알림 하나에 여러 메시지가 포함될 수 있으므로 전체 배치를 처리한다.
        // 샘플 참조는 콜백 중에만 유효하며, 이 스코프를 나가면 RTI에 대여를 반환한다.
        auto samples = reader_->take();
        for (const auto& sample : samples) {
            if (stopping.load()) break;
            if (sample.info().valid()) {
                auto timestamp = [](const dds::core::Time& time) -> std::optional<std::int64_t> {
                    if (time.sec() < 0 || time.nanosec() >= 1000000000u) return std::nullopt;
                    return static_cast<std::int64_t>(time.sec()) * 1000000000LL + time.nanosec();
                };
                const auto reception = timestamp(sample.info()->reception_timestamp());
                const DdsSampleInfo info{
                    reception ? std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(*reception)))
                              : std::chrono::system_clock::now(),
                    timestamp(sample.info().source_timestamp())};
                SampleInfoScope scope(info);
                deliver(&sample.data());
            }
        }
    }

    void Dispose()
    {
        // 수신 루프가 끝난 뒤만 호출한다. 리스너 연결을 먼저 제거하고 Reader, Topic 순서로 해제한다.
        if (reader_) {
            reader_->listener(nullptr, dds::core::status::StatusMask::none());
            reader_->close();
            reader_.reset();
        }
        topic_ = dds::core::null;
    }
private:
    dds::topic::Topic<T> topic_;
    std::optional<dds::sub::DataReader<T>> reader_;
};
}
