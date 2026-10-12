#pragma once
#include "dds_cpp/transport/RtiTopic.hpp"
#include <chrono>
#include "dds_cpp/api/DdsTimeoutException.hpp"

namespace dds_cpp::detail {
class IRtiWriter {
public:
    virtual ~IRtiWriter() = default;
    virtual void Write(const void* sample) = 0;
    virtual int MatchedCount() = 0;
    virtual void WaitForAcknowledgments(std::chrono::milliseconds timeout) = 0;
};

template <class T>
class RtiWriter final : public IRtiWriter {
public:
    RtiWriter(
        const dds::domain::DomainParticipant& participant,
        const dds::pub::Publisher& publisher,
        dds::core::QosProvider& qos_provider,
        const TopicDefinition& definition,
        std::mutex& topic_mutex)
        : topic_(GetOrCreateTopic<T>(participant, definition.name, topic_mutex))
        , writer_(publisher, topic_, qos_provider.datawriter_qos(ResolveQosProfile(definition)))
    {
    }

    int MatchedCount() override { return writer_.publication_matched_status().current_count(); }
    void WaitForAcknowledgments(std::chrono::milliseconds timeout) override {
        try { writer_.wait_for_acknowledgments(dds::core::Duration::from_millisecs(timeout.count())); }
        catch (const dds::core::TimeoutError& error) { throw DdsTimeoutException(error.what()); }
    }
    void Write(const void* sample) override
    {
        writer_.write(*static_cast<const T*>(sample));
    }

private:
    dds::topic::Topic<T> topic_;
    dds::pub::DataWriter<T> writer_;
};

} // namespace dds_cpp::detail
