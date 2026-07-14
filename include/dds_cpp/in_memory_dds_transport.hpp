#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "dds_cpp/dds_transport.hpp"

namespace dds_cpp {

class DDSCPP_API InMemoryDdsTransport final : public DdsTransport {
public:
    InMemoryDdsTransport();
    ~InMemoryDdsTransport() override;

    void create_publisher(const TopicDefinition& topic, std::string sample_type_name) override;
    std::unique_ptr<Subscription> subscribe(
        const TopicDefinition& topic,
        std::string sample_type_name,
        SampleHandler handler) override;
    void publish(const TopicDefinition& topic, std::string sample_type_name, ByteSpan sample) override;
    void create_typed_publisher(const TopicDefinition& topic, const std::type_info& sample_type) override;
    std::unique_ptr<Subscription> subscribe_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler) override;
    void publish_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) override;

private:
    struct State;
    class InMemorySubscription;
    std::shared_ptr<State> state_;
};

} // namespace dds_cpp
